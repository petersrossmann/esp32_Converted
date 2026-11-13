/*
 * dht22.c — DHT22/AM2301 sensor, ported from the old ESP8266 user/dht22.c.
 * Thin wrapper over esp-idf-lib/dht (replaces the bit-banged ESP8266 driver).
 */
#include "dht22.h"
#include "dht.h"                 /* esp-idf-lib/dht component header */
#include "driver/gpio.h"
#include "esp_log.h"
#include "config.h"

static const char *TAG = "dht22";

/* Expose DHT_DATA_GPIO so it can be referenced from config.h (or vice versa) */
#ifndef DHT_DATA_GPIO
#define DHT_DATA_GPIO GPIO_NUM_0
#endif

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

    float h = 0.0f, t = 0.0f;
    esp_err_t err = dht_read_float_data(DHT_TYPE_AM2301, DHT_DATA_GPIO, &h, &t);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "dht_read_float_data failed: %d", err);
        return err;
    }

    /* Old firmware applied offsets from config (temperature in tenths of a
     * degree, humidity in tenths of a percent). Kept for behaviour parity. */
    *temperature = t;
    *humidity = h;
    return ESP_OK;
}
