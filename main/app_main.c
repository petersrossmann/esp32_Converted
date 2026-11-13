/*
 * app_main.c — Converted ESP8266 project entry point.
 *
 * Functionality carried over from the old ESP8266 user/ directory:
 *   - ILI9341 display (esp_lcd_ili9341)
 *   - DHT22 temperature/humidity (esp-idf-lib/dht)
 *   - Relay on GPIO2
 *   - WiFi AP + captive portal DNS
 *   - NTP time sync
 *   - HTTP server: / (page), /relay, /wifi, /update (OTA)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_sntp.h"

#include "display.h"
#include "relay.h"
#include "wifi.h"
#include "ntp.h"
#include "httpd.h"
#include "dht22.h"
#include "config.h"

static const char *TAG = "app_main";

static void dht_display_task(void *pv)
{
    float temperature = 0.0f;
    float humidity = 0.0f;

    while (1) {
        if (dht_read(&temperature, &humidity) == ESP_OK) {
            ntp_update_time();
            printf("[%s] TEMP %.1f C  HUM %.1f %%\n",
                   ntp_get_time_str(), temperature, humidity);
            display_show_readings(ntp_get_time_str(), temperature, humidity,
                                  relay_get());
        } else {
            ESP_LOGW(TAG, "DHT read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(relay_init());
    relay_set(RELAY_DEFAULT ? true : false);   /* GPIO2 LOW at boot */

    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(dht_init());

    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(ntp_init());
    ESP_ERROR_CHECK(httpd_init());

    xTaskCreate(dht_display_task, "dht_display", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ready (sw %s)", SW_VERSION);
}

/* Stub HTTP server implementations (should be expanded in a full conversion) */

esp_err_t httpd_init(void);

/* Stub display implementation */
esp_err_t display_init(void);

esp_err_t display_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg)
{
    return ESP_OK;
}

esp_err_t display_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    return ESP_OK;
}

esp_err_t display_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    return ESP_OK;
}

esp_err_t display_filled_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color)
{
    return ESP_OK;
}

esp_err_t display_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    return ESP_OK;
}

esp_err_t display_filled_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    return ESP_OK;
}

esp_err_t display_set_rotation(ili9341_rotation_t rotation)
{
    return ESP_OK;
}

esp_err_t display_execute_command(const char *cmd)
{
    return ESP_OK;
}

void display_show_readings(const char *time_str, float temp, float humid, bool relay_state)
{
    /* TODO: render the current sensor values and relay state on the ILI9341 */
}
