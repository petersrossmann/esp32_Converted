/*
 * display.c — ILI9341 display driver ported from ESP8266 user/display.c.
 */
#include "display.h"
#include "config.h"
#include "config_store.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_ili9341.h"
#include "fonts.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "ntp.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "display";
static esp_lcd_panel_handle_t s_panel = NULL;

static char s_display_buf[512] = "T[0,0,2,0,65000]Hallo World";
#define MAX_PARAMETERS 10
#define TOP_BG_COLOR   0x001F  /* Blau (entspricht 31 im Bash-Skript) */

static bool s_backlight_on = false;
static TaskHandle_t s_blink_task = NULL;
static SemaphoreHandle_t s_dma_done_sem = NULL;
static SemaphoreHandle_t s_display_mutex = NULL;
static TimerHandle_t s_bl_timer = NULL;

static uint16_t *s_line_buf = NULL;
#define MAX_LINE_PIXELS (ILI9341_WIDTH * 32)

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    if (s_dma_done_sem) {
        xSemaphoreGiveFromISR(s_dma_done_sem, &high_task_awoken);
    }
    return (high_task_awoken == pdTRUE);
}

static void wait_dma_done(void)
{
    if (s_dma_done_sem) {
        xSemaphoreTake(s_dma_done_sem, pdMS_TO_TICKS(100));
    }
}

static inline uint16_t swap16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}

static void set_backlight(bool on)
{
    if (s_backlight_on != on) {
        gpio_set_level(ILI9341_BL_GPIO, on ? 1 : 0);
        s_backlight_on = on;
    }
}

static bool check_display_activity(void)
{
    int hour, minute, wday;
    uint8_t sh, sm, eh, em;
    config_store_get_display_activity(&sh, &sm, &eh, &em);
    ntp_get_actual_time(&hour, &minute, &wday);
    if (hour < sh) return false;
    if (hour == sh && minute < sm) return false;
    if (hour > eh) return false;
    if (hour == eh && minute > em) return false;
    return true;
}

static void bl_timer_cb(TimerHandle_t xTimer)
{
    set_backlight(false);
    ESP_LOGI(TAG, "Display-Beleuchtung nach Timeout ausgeschaltet");
}

static void trigger_backlight_activity(void)
{
    if (!check_display_activity()) {
        set_backlight(false);
        if (s_bl_timer) {
            xTimerStop(s_bl_timer, 0);
        }
        return;
    }

    set_backlight(true);

    unsigned long timeout_sec = config_store_get_display_timeout();
    ESP_LOGI(TAG, "Backlight an, Timeout = %lu Sekunden", timeout_sec);

    if (s_bl_timer) {
        if (timeout_sec > 0) {
            xTimerChangePeriod(s_bl_timer, pdMS_TO_TICKS(timeout_sec * 1000UL), 0);
        } else {
            xTimerStop(s_bl_timer, 0);
        }
    }
}

static esp_err_t fill_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!s_panel) return ESP_ERR_INVALID_STATE;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > ILI9341_WIDTH)  x1 = ILI9341_WIDTH;
    if (y1 > ILI9341_HEIGHT) y1 = ILI9341_HEIGHT;
    if (x0 >= x1 || y0 >= y1) return ESP_OK;

    int w = x1 - x0;
    int h = y1 - y0;
    uint16_t be_color = swap16(color);

    if (!s_line_buf) {
        s_line_buf = heap_caps_malloc(MAX_LINE_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (!s_line_buf) return ESP_ERR_NO_MEM;
    }

    int max_rows = MAX_LINE_PIXELS / w;
    if (max_rows < 1) max_rows = 1;
    if (max_rows > h) max_rows = h;

    int fill_pixels = w * max_rows;
    for (int i = 0; i < fill_pixels; i++) {
        s_line_buf[i] = be_color;
    }

    esp_err_t ret = ESP_OK;
    int y = y0;
    while (y < y1) {
        int rows = y1 - y;
        if (rows > max_rows) rows = max_rows;

        ret = esp_lcd_panel_draw_bitmap(s_panel, x0, y, x1, y + rows, s_line_buf);
        if (ret != ESP_OK) break;
        wait_dma_done();

        y += rows;
    }
    return ret;
}

static void clear_display(void)
{
    fill_rect(0, 0, ILI9341_WIDTH, ILI9341_HEIGHT, 0x0000);
}

static TM_FontDef_t *select_font(int font_id)
{
    switch (font_id) {
        case 0: return &TM_Font_7x10;
        case 1: return &TM_Font_11x18;
        default: return &TM_Font_16x26;
    }
}

static void draw_str_bg(int x, int y, const char *str, TM_FontDef_t *font, uint16_t fg, uint16_t bg)
{
    if (!s_panel || !str) return;
    if (!s_line_buf) {
        s_line_buf = heap_caps_malloc(MAX_LINE_PIXELS * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (!s_line_buf) return;
    }

    int h = font->FontHeight;
    int w = font->FontWidth;
    int adv = w + 1;
    uint16_t fg_be = swap16(fg);
    uint16_t bg_be = swap16(bg);

    int len = (int)strlen(str);
    if (len == 0) return;

    int total_w = len * adv;
    if (x + total_w > ILI9341_WIDTH) {
        total_w = ILI9341_WIDTH - x;
    }
    if (total_w <= 0 || y < 0 || y + h > ILI9341_HEIGHT) return;

    for (int i = 0; i < total_w * h; i++) {
        s_line_buf[i] = bg_be;
    }

    int cx = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c < 32 || c > 127) c = '?';
        int idx = c - 32;

        for (int row = 0; row < h; row++) {
            uint16_t row_data = font->data[idx * h + row];
            for (int col = 0; col < w; col++) {
                if (cx + col < total_w && (row_data & (1 << (15 - col)))) {
                    s_line_buf[row * total_w + (cx + col)] = fg_be;
                }
            }
        }
        cx += adv;
    }

    esp_lcd_panel_draw_bitmap(s_panel, x, y, x + total_w, y + h, s_line_buf);
    wait_dma_done();
}

static int parse_parameters(const char *line, char *params[])
{
    const char *start = strchr(line, '[');
    const char *end   = strchr(line, ']');
    if (!start || !end || end <= start) return 0;

    int count = 0;
    const char *p = start + 1;
    while (p < end && count < MAX_PARAMETERS - 1) {
        const char *comma = memchr(p, ',', (size_t)(end - p));
        const char *tok_end = comma ? comma : end;
        size_t len = (size_t)(tok_end - p);
        params[count] = malloc(len + 1);
        if (params[count]) {
            memcpy(params[count], p, len);
            params[count][len] = '\0';
        }
        count++;
        if (!comma) break;
        p = comma + 1;
    }

    params[count] = strdup(end + 1);
    if (params[count]) count++;

    return count;
}

void display_set(const char *text)
{
    if (!text) return;
    if (s_display_mutex && xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGW(TAG, "display_set: mutex timeout");
        return;
    }

    int len = strlen(text);
    if (len < (int)sizeof(s_display_buf))
        strcpy(s_display_buf, text);
    else
        strcpy(s_display_buf, "oops");

    char *params[MAX_PARAMETERS];
    for (int i = 0; i < MAX_PARAMETERS; i++) params[i] = NULL;
    int result = parse_parameters(s_display_buf, params);
    long nparams[MAX_PARAMETERS];
    for (int k = 0; k < result; k++) nparams[k] = params[k] ? atol(params[k]) : -1;

    switch (s_display_buf[0]) {
         case 'T':
            if (result >= 5 && params[result - 1]) {
                long x = *nparams;
                long y = *(nparams + 1);
                int font_id = (int)*(nparams + 2);
                uint16_t fg = (uint16_t)*(nparams + 3);
                uint16_t bg = (uint16_t)*(nparams + 4);
                TM_FontDef_t *font = select_font(font_id);
                draw_str_bg((int)x, (int)y, params[result - 1], font, fg, bg);
            }
            break;
        case 'x': case 'X':
            clear_display();
            fill_rect(0, 0, ILI9341_WIDTH, 100, TOP_BG_COLOR);
            break;
        case 'l': case 'L':
            if (result >= 5)
                draw_str_bg((int)nparams[0], (int)nparams[1], "Line", select_font(0), (uint16_t)nparams[4], 0x0000);
            break;
        case 'c': case 'C':
            if (result >= 4)
                fill_rect((int)nparams[0], (int)nparams[1],
                          (int)(nparams[0] + nparams[2] * 2), (int)(nparams[1] + nparams[2] * 2), (uint16_t)nparams[3]);
            break;
        case 'r': case 'R':
            if (result >= 5)
                fill_rect((int)nparams[0], (int)nparams[1],
                          (int)(nparams[0] + nparams[2]), (int)(nparams[1] + nparams[3]), (uint16_t)nparams[4]);
            break;
        default:
            ESP_LOGW(TAG, "unknown display cmd: %c", s_display_buf[0]);
            break;
    }

    trigger_backlight_activity();

    for (int i = 0; i < MAX_PARAMETERS; i++) free(params[i]);

    if (s_display_mutex) {
        xSemaphoreGive(s_display_mutex);
    }
}

const char *display_get(void) { return s_display_buf; }

static void bl_task(void *pv)
{
    vTaskDelay(pdMS_TO_TICKS(3000));
    while (1) {
        if (!check_display_activity()) {
            set_backlight(false);
            if (s_bl_timer) {
                xTimerStop(s_bl_timer, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

esp_err_t display_init(void)
{
    if (!s_dma_done_sem) {
        s_dma_done_sem = xSemaphoreCreateBinary();
    }
    if (!s_display_mutex) {
        s_display_mutex = xSemaphoreCreateMutex();
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = ILI9341_MOSI_GPIO, .miso_io_num = -1, .sclk_io_num = ILI9341_CLK_GPIO,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = ILI9341_WIDTH * ILI9341_HEIGHT * 2 + 8,
    };
    esp_err_t ret = spi_bus_initialize(ILI9341_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "SPI bus: %d", ret); return ret; }

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = ILI9341_DC_GPIO,
        .cs_gpio_num = ILI9341_CS_GPIO,
        .spi_mode = 0,
        .pclk_hz = SPI_SPEED_HZ,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_color_trans_done,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ILI9341_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Panel IO: %d", ret); return ret; }

    ili9341_vendor_config_t vc = {0};
    esp_lcd_panel_dev_config_t pc = { .reset_gpio_num = -1, .rgb_endian = LCD_RGB_ENDIAN_BGR,
                                      .bits_per_pixel = 16, .vendor_config = &vc };
    ret = esp_lcd_new_panel_ili9341(io_handle, &pc, &s_panel);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "Panel dev: %d", ret); return ret; }

    esp_lcd_panel_reset(s_panel); esp_lcd_panel_init(s_panel);
    esp_lcd_panel_mirror(s_panel, false, false); esp_lcd_panel_swap_xy(s_panel, true);
    esp_lcd_panel_disp_on_off(s_panel, true);
    clear_display();

    fill_rect(0, 0, ILI9341_WIDTH, 100, TOP_BG_COLOR);

    gpio_config_t bl = { .pin_bit_mask = 1ULL << ILI9341_BL_GPIO, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&bl);

    s_bl_timer = xTimerCreate("bl_timer", pdMS_TO_TICKS(60000), pdFALSE, NULL, bl_timer_cb);
    trigger_backlight_activity();

    xTaskCreate(bl_task, "bl_task", 2048, NULL, 4, &s_blink_task);
    ESP_LOGI(TAG, "ILI9341 ready"); return ESP_OK;
}

void display_show_readings(const char *time_str, float temp, float humid, bool relay_state)
{
    if (!s_panel) return;
    if (s_display_mutex && xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return;
    }

    char line[64];

    snprintf(line, sizeof(line), "Zeit: %s   ",
             (time_str && strlen(time_str) > 0) ? time_str : "--:--:--");
    draw_str_bg(10, 5, line, &TM_Font_11x18, 0xFFFF, TOP_BG_COLOR);

    snprintf(line, sizeof(line), "Innen: %.2f\177C   ", temp);
    draw_str_bg(10, 28, line, &TM_Font_11x18, 0x07E0, TOP_BG_COLOR);

    if (humid >= 0.0f) {
        snprintf(line, sizeof(line), "Hum:   %.1f %%   ", humid);
        draw_str_bg(10, 51, line, &TM_Font_11x18, 0x07FF, TOP_BG_COLOR);
    }

    snprintf(line, sizeof(line), "Relais: %s ", relay_state ? "EIN" : "AUS");
    draw_str_bg(10, 74, line, &TM_Font_11x18, relay_state ? 0xF800 : 0xC618, TOP_BG_COLOR);

    if (s_display_mutex) {
        xSemaphoreGive(s_display_mutex);
    }
}

esp_err_t display_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    if (!s_panel || !text) return ESP_ERR_INVALID_STATE;
    if (s_display_mutex && xSemaphoreTake(s_display_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    draw_str_bg(x, y, text, select_font(1), fg, bg);

    if (s_display_mutex) {
        xSemaphoreGive(s_display_mutex);
    }
    return ESP_OK;
}
