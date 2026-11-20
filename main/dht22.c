/*
 * dht22.c — DHT22/AM2301 sensor, ported from the old ESP8266 user/dht22.c.
 * Thin wrapper over esp-idf-lib/dht (replaces the bit-banged ESP8266 driver).
 */
#include "dht22.h"
#include "dht.h"                 /* esp-idf-lib/dht component header */
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"
#include "config_store.h"

static const char *TAG = "dht22";

#ifndef DHT_DATA_GPIO
#define DHT_DATA_GPIO GPIO_NUM_0
#endif

static float s_last_temp = 0.0f;
static float s_last_hum  = 0.0f;
static bool  s_has_valid = false;
static TickType_t s_last_read_tick = 0;

esp_err_t dht_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DHT_DATA_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %d", err);
        return err;
    }
    gpio_set_level(DHT_DATA_GPIO, 1);
    return ESP_OK;
}

esp_err_t dht_read(float *temperature, float *humidity)
{
    if (!temperature || !humidity) {
        return ESP_ERR_INVALID_ARG;
    }

    TickType_t now = xTaskGetTickCount();

    /* Wenn letzte gültige Messung < 2.5s her ist (z.B. Webseiten-Aufruf), Cache zurückgeben */
    if (s_has_valid && (now - s_last_read_tick) < pdMS_TO_TICKS(2500)) {
        *temperature = s_last_temp + config_store_get_temperature_offset() / 10.0f;
        *humidity    = s_last_hum  + config_store_get_humidity_offset() / 10.0f;
        return ESP_OK;
    }

    float h = 0.0f, t = 0.0f;
    esp_err_t err = dht_read_float_data(DHT_TYPE_AM2301, DHT_DATA_GPIO, &h, &t);

    /* Bei sporadischem Interrupt-Timeout einmal nach 2.1s wiederholen */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "dht_read retry after error: %d", err);
        vTaskDelay(pdMS_TO_TICKS(2100));
        err = dht_read_float_data(DHT_TYPE_AM2301, DHT_DATA_GPIO, &h, &t);
    }

    s_last_read_tick = xTaskGetTickCount();

    if (err == ESP_OK) {
        s_last_temp = t;
        s_last_hum  = h;
        s_has_valid = true;
    } else {
        ESP_LOGW(TAG, "dht_read_float_data failed after retry: %d", err);
        if (!s_has_valid) {
            return err;
        }
        /* Bei Fehler letzten gültigen Messwert verwenden */
    }

    /* Apply offsets from config_store (tenths of a degree/percent) */
    *temperature = s_last_temp + config_store_get_temperature_offset() / 10.0f;
    *humidity    = s_last_hum  + config_store_get_humidity_offset() / 10.0f;
    return ESP_OK;
}
