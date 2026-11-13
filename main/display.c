/*
 * display.c — ILI9341 display driver (stub, using ESP-IDF esp_lcd_ili9341 component).
 *
 * Functionality ported from the old ESP8266 user/display.c:
 *   - Command parsing of strings like "T[x,y,width,height,color]text" (unsupported now)
 *   - Real-time temperature/humidity bar rendering on ILI9341
 *   - Background LED control (GPIO19)
 */
#include "display.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

/* Lightweight SPI bus configuration for ILI9341 */
static spi_bus_config_t buscfg = {
    .mosi_io_num = ILI9341_MOSI_GPIO,
    .miso_io_num = -1,
    .sclk_io_num = ILI9341_CLK_GPIO,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = ILI9341_WIDTH * ILI9341_HEIGHT * 2 + 8,
};

void display_cleanup(void)
{
    /* Clean up ILI9341 */
    // TODO: remove panel
}

esp_err_t display_show_readings(const char *time_str, float temp, float humid, bool relay_state)
{
    /* TODO: implement rendering of time, temperature, humidity, and relay state on the ILI9341 */
    return ESP_OK;
}