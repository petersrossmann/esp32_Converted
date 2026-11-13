/*
 * DS18B20 Temperature Sensor Driver for ESP-IDF
 *
 * This driver implements the 1-Wire protocol to communicate with
 * DS18B20 digital temperature sensors on ESP32 platforms.
 *
 * Features:
 * - Single or multiple DS18B0 sensors on same bus
 * - Support for parasite power mode
 * - High precision temperature readings (0.0625°C)
 * - Conversion time configurable (9-bit to 12-bit resolution)
 */

#ifndef __DS18B20_H__
#define __DS18B20_H__

#include <driver/gpio.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DS18B20 sensor type definitions
 */
typedef enum
{
    DS18B20_9_BIT = 9,   //!< 9-bit resolution, 93.75ms conversion time
    DS18B20_10_BIT = 10, //!< 10-bit resolution, 187.5ms conversion time
    DS18B20_11_BIT = 11, //!< 11-bit resolution, 375ms conversion time
    DS18B20_12_BIT = 12  //!< 12-bit resolution, 750ms conversion time
} ds18b20_resolution_t;

/**
 * @brief Initialize DS18B20 sensor
 *
 * @param gpio_num GPIO pin connected to DS18B20 data line (with pull-up resistor)
 * @param resolution Desired temperature conversion resolution
 * @return ESP_OK on success
 */
esp_err_t ds18b20_init(gpio_num_t gpio_num, ds18b20_resolution_t resolution);

/**
 * @brief Measure temperature from DS18B20 sensor
 *
 * @param temperature Pointer to store temperature in degrees Celsius (float)
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if conversion timed out
 */
esp_err_t ds18b20_measure_temperature(float *temperature);

/**
 * @brief Set DS18B20 sensor resolution
 *
 * @param resolution Desired temperature conversion resolution
 * @return ESP_OK on success
 */
esp_err_t ds18b20_set_resolution(ds18b20_resolution_t resolution);

/**
 * @brief Get current DS18B20 sensor resolution
 *
 * @return Current resolution value
 */
ds18b20_resolution_t ds18b20_get_resolution(void);

/**
 * @brief Reset all DS18B20 sensors on the bus
 *
 * @return ESP_OK on success
 */
esp_err_t ds18b20_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif  // __DS18B20_H__