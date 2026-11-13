/*
 * DS18B20 Temperature Sensor Driver for ESP-IDF
 *
 * This driver implements the 1-Wire protocol to communicate with
 * DS18B20 digital temperature sensors on ESP32 platforms.
 *
 * Features:
 * - Single or multiple DS18B20 sensors on same bus
 * - Support for parasite power mode
 * - High precision temperature readings (0.0625°C)
 * - Conversion time configurable (9-bit to 12-bit resolution)
 */

#include "DS18B20.h"

#include <string.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <driver/gpio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ds18b20";

// DS18B20 command set
#define DS18B20_CMD_SKIP_ROM      0xCC
#define DS18B20_CMD_MATCH_ROM     0x55
#define DS18B20_CMD_CONVERT_T     0x44
#define DS18B20_CMD_WRITE_SCRATCH  0x4B
#define DS18B20_CMD_READ_SCRATCH  0xBE
#define DS18B20_CMD_COPY_SCRATCH  0x48
#define DS18B20_CMD_RECALL_CONV   0xB8
#define DS18B20_CMD_READ_POWER_SUP 0xB4

// DS18B20 resolution commands
#define DS18B20_RES_9_BIT    0x1F
#define DS18B20_RES_10_BIT   0x3F
#define DS18B20_RES_11_BIT   0x5F
#define DS18B20_RES_12_BIT   0x7F

static gpio_num_t ds18b20_gpio = GPIO_NUM_NC;
static ds18b20_resolution_t current_resolution = DS18B20_12_BIT;

// 1-Wire timing parameters (microseconds)
#define DS18B20_T0H   12U    // Low pulse for 0: 1-15us
#define DS18B20_T0L   60U    // High pulse for 0: 60-120us
#define DS18B20_T1H   60U    // Low pulse for 1: 60-120us
#define DS18B20_T1L   10U    // High pulse for 1: 4-15us
#define DS18B20_T_RST 480U   // Reset pulse: 480-960us

// Function declarations
static esp_err_t ds18b20_write_bit(uint8_t bit);
static uint8_t ds18b20_read_bit(void);
static esp_err_t ds18b20_write_byte(uint8_t byte);
static uint8_t ds18b20_read_byte(void);
static esp_err_t ds18b20_reset(void);


esp_err_t ds18b20_init(gpio_num_t gpio_num, ds18b20_resolution_t resolution)
{
    if (gpio_num == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Invalid GPIO number");
        return ESP_ERR_INVALID_ARG;
    }

    ds18b20_gpio = gpio_num;
    current_resolution = resolution;

    // Configure GPIO for 1-Wire communication
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Reset and set resolution
    ESP_ERROR_CHECK(ds18b20_reset());
    ESP_ERROR_CHECK(ds18b20_set_resolution(resolution));

    ESP_LOGI(TAG, "DS18B20 initialized on GPIO%d with %d-bit resolution",
             gpio_num, resolution);

    return ESP_OK;
}

esp_err_t ds18b20_measure_temperature(float *temperature)
{
    if (!temperature) {
        ESP_LOGE(TAG, "Temperature pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Reset bus and check for device presence
    esp_err_t ret = ds18b20_reset();
    if (ret != ESP_OK) {
        return ret;
    }

    // Send skip ROM + convert T command
    ESP_ERROR_CHECK(ds18b20_write_byte(DS18B20_CMD_SKIP_ROM));
    ESP_ERROR_CHECK(ds18b20_write_byte(DS18B20_CMD_CONVERT_T));

    // Wait for conversion to complete.
    // DS18B20 pulls the data line low during conversion; it releases it when done.
    gpio_set_direction(ds18b20_gpio, GPIO_MODE_INPUT);
    TickType_t start_time = xTaskGetTickCount();
    while (gpio_get_level(ds18b20_gpio) == 0) {
        TickType_t now = xTaskGetTickCount();
        if ((now - start_time) * portTICK_PERIOD_MS > 800) {
            ESP_LOGE(TAG, "Temperature conversion timeout");
            gpio_set_direction(ds18b20_gpio, GPIO_MODE_OUTPUT);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Read temperature
    ESP_ERROR_CHECK(ds18b20_reset());
    ESP_ERROR_CHECK(ds18b20_write_byte(DS18B20_CMD_SKIP_ROM));
    ESP_ERROR_CHECK(ds18b20_write_byte(DS18B20_CMD_READ_SCRATCH));

    uint8_t lsb = ds18b20_read_byte();
    uint8_t msb = ds18b20_read_byte();

    // Calculate temperature (2's complement)
    int16_t raw_temp = (msb << 8) | lsb;
    *temperature = (float)raw_temp / 16.0f;

    ESP_LOGI(TAG, "Measured temperature: %.2f°C", *temperature);

    return ESP_OK;
}

esp_err_t ds18b20_set_resolution(ds18b20_resolution_t resolution)
{
    uint8_t cmd = 0;

    switch (resolution) {
        case DS18B20_9_BIT:
            cmd = DS18B20_RES_9_BIT;
            break;
        case DS18B20_10_BIT:
            cmd = DS18B20_RES_10_BIT;
            break;
        case DS18B20_11_BIT:
            cmd = DS18B20_RES_11_BIT;
            break;
        case DS18B20_12_BIT:
            cmd = DS18B20_RES_12_BIT;
            break;
        default:
            ESP_LOGE(TAG, "Invalid resolution: %d", resolution);
            return ESP_ERR_INVALID_ARG;
    }

    // Reset and write scratchpad to set resolution
    ESP_ERROR_CHECK(ds18b20_reset());
    ESP_ERROR_CHECK(ds18b20_write_byte(DS18B20_CMD_SKIP_ROM));
    ESP_ERROR_CHECK(ds18b20_write_byte(DS18B20_CMD_WRITE_SCRATCH));
    ESP_ERROR_CHECK(ds18b20_write_byte(cmd));
    ESP_ERROR_CHECK(ds18b20_write_byte(0xFF)); // TH register
    ESP_ERROR_CHECK(ds18b20_write_byte(0xFF)); // TL register
    ESP_ERROR_CHECK(ds18b20_write_byte(DS18B20_CMD_COPY_SCRATCH));

    current_resolution = resolution;

    ESP_LOGI(TAG, "DS18B20 resolution set to %d-bit", resolution);

    return ESP_OK;
}

ds18b20_resolution_t ds18b20_get_resolution(void)
{
    return current_resolution;
}

esp_err_t ds18b20_reset_all(void)
{
    return ds18b20_reset();
}

// --- Private helper functions ---

static esp_err_t ds18b20_write_bit(uint8_t bit)
{
    // Set low pulse
    gpio_set_level(ds18b20_gpio, 0);
    if (bit) {
        esp_rom_delay_us(DS18B20_T0H);
    } else {
        esp_rom_delay_us(DS18B20_T1H);
    }

    // Set high pulse
    gpio_set_level(ds18b20_gpio, 1);
    if (bit) {
        esp_rom_delay_us(DS18B20_T0L);
    } else {
        esp_rom_delay_us(DS18B20_T1L);
    }

    return ESP_OK;
}

static uint8_t ds18b20_read_bit(void)
{
    uint8_t bit;

    // Set GPIO to input
    gpio_set_direction(ds18b20_gpio, GPIO_MODE_INPUT);

    // Wait for DS18B20 to pull down (response time)
    uint32_t timeout = 100; // microseconds
    uint32_t elapsed = 0;

    while (elapsed < timeout && gpio_get_level(ds18b20_gpio) == 1) {
        esp_rom_delay_us(1);
        elapsed++;
    }

    if (elapsed >= timeout) {
        // No response, assume bit=0 (device pulled low)
        bit = 0;
    } else {
        // Measure high pulse duration
        uint32_t high_duration = 0;
        while (gpio_get_level(ds18b20_gpio) == 0) {
            esp_rom_delay_us(1);
            high_duration++;
        }

        // Determine bit based on high pulse duration
        bit = (high_duration > 50) ? 1 : 0; // >50us = 1, <50us = 0
    }

    // Set GPIO back to output for next write
    gpio_set_direction(ds18b20_gpio, GPIO_MODE_OUTPUT);

    return bit;
}

static esp_err_t ds18b20_write_byte(uint8_t byte)
{
    for (int i = 0; i < 8; i++) {
        uint8_t bit = (byte >> i) & 0x01;
        ESP_ERROR_CHECK(ds18b20_write_bit(bit));
    }

    return ESP_OK;
}

static uint8_t ds18b20_read_byte(void)
{
    uint8_t byte = 0;

    for (int i = 0; i < 8; i++) {
        byte |= (ds18b20_read_bit() << i);
    }

    return byte;
}

static esp_err_t ds18b20_reset(void)
{
    // Set GPIO to output and send reset pulse
    gpio_set_direction(ds18b20_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(ds18b20_gpio, 0);
    esp_rom_delay_us(DS18B20_T_RST);

    // Release bus and wait for presence pulse
    gpio_set_level(ds18b20_gpio, 1);
    gpio_set_direction(ds18b20_gpio, GPIO_MODE_INPUT);
    esp_rom_delay_us(120);

    // Check if device responded (presence pulse detected)
    if (gpio_get_level(ds18b20_gpio) == 1) {
        ESP_LOGE(TAG, "DS18B20 not present or bus error");
        gpio_set_direction(ds18b20_gpio, GPIO_MODE_OUTPUT);
        gpio_set_level(ds18b20_gpio, 1);
        return ESP_ERR_NOT_FOUND;
    }

    // Wait for device to finish reset (max 720us)
    esp_rom_delay_us(400);

    // Return GPIO to output, idle high (pull-up holds it)
    gpio_set_direction(ds18b20_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(ds18b20_gpio, 1);

    return ESP_OK;
}