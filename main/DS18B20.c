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
#include "config_store.h"

static const char *TAG = "ds18b20";

// DS18B20 command set
#define DS18B20_CMD_SKIP_ROM      0xCC
#define DS18B20_CMD_MATCH_ROM     0x55
#define DS18B20_CMD_CONVERT_T     0x44
#define DS18B20_CMD_WRITE_SCRATCH  0x4E  // Fixed from 0x4B
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

static portMUX_TYPE ds18b20_spinlock = portMUX_INITIALIZER_UNLOCKED;

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

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio_num,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD, // Prevents bus shorts
        .pull_up_en = GPIO_PULLUP_ENABLE,  // Note: Also use an external 4.7kΩ pull-up resistor
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_set_level(gpio_num, 1); // Idle high (released)
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    // Reset and set resolution
    // Reset and set resolution
    esp_err_t err = ds18b20_reset();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 not found on GPIO%d, skipping initialization", gpio_num);
        return err;
    }

    err = ds18b20_set_resolution(resolution);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set DS18B20 resolution on GPIO%d", gpio_num);
        return err;
    }

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

    // Ensure pin is in Open-Drain Input/Output mode
    gpio_set_direction(ds18b20_gpio, GPIO_MODE_INPUT_OUTPUT_OD);

    // Reset bus and check for device presence
    esp_err_t ret = ds18b20_reset();
    if (ret != ESP_OK) {
        return ret;
    }

    // Send skip ROM + convert T command (never use ESP_ERROR_CHECK on sensor I/O)
    if ((ret = ds18b20_write_byte(DS18B20_CMD_SKIP_ROM)) != ESP_OK) return ret;
    if ((ret = ds18b20_write_byte(DS18B20_CMD_CONVERT_T)) != ESP_OK) return ret;

    // Release line (HIGH in open-drain) and wait for conversion to complete
    gpio_set_level(ds18b20_gpio, 1);
    TickType_t start_time = xTaskGetTickCount();
    while (gpio_get_level(ds18b20_gpio) == 0) {
        TickType_t now = xTaskGetTickCount();
        if ((now - start_time) * portTICK_PERIOD_MS > 800) {
            ESP_LOGE(TAG, "Temperature conversion timeout");
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Read temperature (return error cleanly instead of aborting)
    if ((ret = ds18b20_reset()) != ESP_OK) {
        ESP_LOGW(TAG, "Second reset failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if ((ret = ds18b20_write_byte(DS18B20_CMD_SKIP_ROM)) != ESP_OK) return ret;
    if ((ret = ds18b20_write_byte(DS18B20_CMD_READ_SCRATCH)) != ESP_OK) return ret;

    uint8_t lsb = ds18b20_read_byte();
    uint8_t msb = ds18b20_read_byte();

    // Calculate temperature (2's complement)
    int16_t raw_temp = (msb << 8) | lsb;
    *temperature = (float)raw_temp / 16.0f + config_store_get_temperature_offset() / 10.0f;

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
    portENTER_CRITICAL(&ds18b20_spinlock);
    gpio_set_level(ds18b20_gpio, 0);
    esp_rom_delay_us(480);
    gpio_set_level(ds18b20_gpio, 1); // Release bus (open-drain)
    esp_rom_delay_us(70);            // Wait 70us to sample inside the 60-240us presence window
    int presence = gpio_get_level(ds18b20_gpio);
    portEXIT_CRITICAL(&ds18b20_spinlock);

    esp_rom_delay_us(410);           // Complete the 480us recovery slot

    if (presence != 0) {
        ESP_LOGE(TAG, "DS18B20 not present or bus error");
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static esp_err_t ds18b20_write_bit(uint8_t bit)
{
    portENTER_CRITICAL(&ds18b20_spinlock);
    if (bit) {
        gpio_set_level(ds18b20_gpio, 0);
        esp_rom_delay_us(6);         // Pull low for 1-15us
        gpio_set_level(ds18b20_gpio, 1); // Release bus
        esp_rom_delay_us(64);        // Wait out remainder of 60us+ slot
    } else {
        gpio_set_level(ds18b20_gpio, 0);
        esp_rom_delay_us(60);        // Hold low for 60-120us
        gpio_set_level(ds18b20_gpio, 1); // Release bus
        esp_rom_delay_us(10);        // Recovery time
    }
    portEXIT_CRITICAL(&ds18b20_spinlock);
    return ESP_OK;
}

static uint8_t ds18b20_read_bit(void)
{
    uint8_t bit;
    portENTER_CRITICAL(&ds18b20_spinlock);
    gpio_set_level(ds18b20_gpio, 0);
    esp_rom_delay_us(3);             // Master initiates read slot (>1us low)
    gpio_set_level(ds18b20_gpio, 1); // Release bus so sensor can drive it
    esp_rom_delay_us(10);            // Sample before 15us from start of slot
    bit = gpio_get_level(ds18b20_gpio);
    portEXIT_CRITICAL(&ds18b20_spinlock);

    esp_rom_delay_us(50);            // Wait out remainder of 60us+ slot
    return bit;
}