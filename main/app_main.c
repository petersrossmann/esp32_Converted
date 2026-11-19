/*
 *
 * app_main.c — Converted ESP8266 project entry point.
 *
 * Functionality carried over from the old ESP8266 user/ directory:
 *   - ILI9341 display (esp_lcd_ili9341)
 *   - DHT22 temperature/humidity (esp-idf-lib/dht)
 *   - Relay on GPIO2
 *   - WiFi AP + captive portal DNS
 *   - NTP time sync
 *   - HTTP server: / (page), /relay, /wifi, /update (OTA)
 *   - DS18B20 temperature sensor (1-Wire on GPIO4)
 */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_ota_ops.h"
#include "esp_log.h"

#ifdef DISPLAY_ENABLED
#include "display.h"
#endif
#ifdef RELAY_ENABLED
#include "relay.h"
#endif
#ifdef WIFI_ENABLED
#include "wifi.h"
#include "esp_wifi.h"
#endif
#ifdef NTP_ENABLED
#include "ntp.h"
#endif
#ifdef HTTPD_ENABLED
#include "httpd.h"
#endif
#include "dht22.h"

#ifdef SENSOR_DS18B20
#include "DS18B20.h"
#endif
#ifdef SENSOR_BME280
#include "bme280.h"
#include "bme280.h"
#endif
#ifdef PORTSTATE_ENABLED
#include "portstate.h"
#endif
#include "ota_client.h"

static const char *TAG = "app_main";

/* Shared globals used by httpd.c and display.c */
#ifdef RELAY_ENABLED
bool g_relay_state = false;
#endif
float g_ds18b20_temperature = 0.0f;
bool g_ds18b20_valid = false;
#ifdef NTP_ENABLED
bool g_ntp_started = false;
#endif

static TaskHandle_t dht_display_task_handle = NULL;

/* Forward declarations */
static void dht_display_task(void *pv);

/* Called when STA gets an IP — kick off NTP sync once. */
#ifdef NTP_ENABLED
static void got_ip_handler(void *arg, esp_event_base_t base,
                           int32_t id, void *data)
{
    /* SoftAP abschalten, um den 192.168.4.1-Subnetzkonflikt zu vermeiden */
    esp_wifi_set_mode(WIFI_MODE_STA);

    if (!g_ntp_started) {
        g_ntp_started = true;
        ntp_start();
    }
}
#endif

/*--------------------------------------------------------------------------*
 * Tasks
 *--------------------------------------------------------------------------*/
static void dht_display_task(void *pv)
{
    float temperature = 0.0f, humidity = -1.0f;
    float ds_temp = 0.0f;
    uint32_t tick = 0;

    while (1) {
#ifdef NTP_ENABLED
        ntp_update_time();
#endif

        /* Sensoren nur alle 8 Sekunden auslesen (DHT22 & DS18B20 schonen) */
        if ((tick % 8) == 0) {
#ifdef SENSOR_DHT22
            if (dht_read(&temperature, &humidity) != ESP_OK) {
                ESP_LOGW(TAG, "DHT read failed");
            }
#endif

#ifdef SENSOR_DS18B20
            if (ds18b20_measure_temperature(&ds_temp) == ESP_OK) {
                g_ds18b20_temperature = ds_temp;
                g_ds18b20_valid = true;
                temperature = ds_temp;
//               ESP_LOGI(TAG, "DS18B20: %.2f C", ds_temp);
            } else {
                g_ds18b20_valid = false;
            }
#endif
        }

#ifdef DISPLAY_ENABLED
        display_show_readings(
#ifdef NTP_ENABLED
            ntp_get_time_str(),
#else
            "--:--:--",
#endif
            temperature,
            humidity,
#ifdef RELAY_ENABLED
            g_relay_state
#else
            false
#endif
        );
#endif

        tick++;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void app_main(void)
{
    /* NVS init */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#if 0
    /* Mount SPIFFS for web content */
    {
        esp_vfs_spiffs_conf_t spiffs_conf = {
            .base_path = "/spiffs",
            .partition_label = NULL,
            .max_files = 5,
            .format_if_mount_failed = false,
        };
        esp_err_t spiffs_ret = esp_vfs_spiffs_register(&spiffs_conf);
        if (spiffs_ret != ESP_OK) {
            ESP_LOGI(TAG, "SPIFFS mount: %d (may already be mounted)", spiffs_ret);
        } else {
            ESP_LOGI(TAG, "SPIFFS mounted at /spiffs");
        }
    }
#endif
    /* Event loop & netifs */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Register IP event handler for NTP */
#ifdef NTP_ENABLED
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &got_ip_handler, NULL));
#endif

    /* Relay init */
#ifdef RELAY_ENABLED
    relay_init();
    relay_set(RELAY_DEFAULT ? true : false);   /* GPIO2 LOW at boot */
#endif

    /* Display init (ILI9341) */
#ifdef DISPLAY_ENABLED
    display_init();
#endif

    /* Sensors */
#ifdef SENSOR_DHT22
    dht_init();
#endif

    /* DS18B20 init on GPIO4 */
#ifdef SENSOR_DS18B20
    ds18b20_init(DS18B20_GPIO, DS18B20_12_BIT);
#endif

#ifdef SENSOR_BME280
    bme280_init();
#endif

#ifdef PORTSTATE_ENABLED
    portstate_init();
#endif

    /* WiFi & captive portal */
#ifdef WIFI_ENABLED
    wifi_init();
#endif

    /* NTP sync */
#ifdef NTP_ENABLED
    ntp_init();
#endif

    /* HTTP server */
#ifdef HTTPD_ENABLED
    httpd_init();
#endif

#ifdef OTA_ENABLED
    ota_client_start();
#endif

    /* Create task */
#ifdef DISPLAY_ENABLED
    xTaskCreate(dht_display_task, "dht_display", 4096, NULL, 5, &dht_display_task_handle);
#endif

    ESP_LOGI(TAG, "ready (sw %s)", esp_app_get_description()->version);
}
