#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* I2C address (0x76 with SDO to GND, 0x77 with SDO to VCC) */
#define BME280_I2C_ADDR 0x76

/* Oversampling */
#define BME280_OS_T_SKP  0x00
#define BME280_OS_T_1    0x01
#define BME280_OS_T_2    0x02
#define BME280_OS_T_4    0x03
#define BME280_OS_T_8    0x04
#define BME280_OS_T_16   0x05

#define BME280_OS_P_SKP  0x00
#define BME280_OS_P_1    0x01
#define BME280_OS_P_2    0x02
#define BME280_OS_P_4    0x03
#define BME280_OS_P_8    0x04
#define BME280_OS_P_16   0x05

#define BME280_OS_H_SKP  0x00
#define BME280_OS_H_1    0x01
#define BME280_OS_H_2    0x02
#define BME280_OS_H_4    0x03
#define BME280_OS_H_8    0x04
#define BME280_OS_H_16   0x05

#define BME280_FILTER_OFF  0x00
#define BME280_FILTER_2    0x01
#define BME280_FILTER_4    0x02
#define BME280_FILTER_8    0x03
#define BME280_FILTER_16   0x04

#define BME280_MODE_SLEEP  0x00
#define BME280_MODE_FORCED 0x01
#define BME280_MODE_NORMAL 0x03

#define BME280_TSB_05  0x00
#define BME280_TSB_62  0x01
#define BME280_TSB_125 0x02
#define BME280_TSB_250 0x03
#define BME280_TSB_500 0x04
#define BME280_TSB_1000 0x05
#define BME280_TSB_10  0x06
#define BME280_TSB_20  0x07

#define SEALEVEL 167 /* meters above sea level for pressure compensation */

esp_err_t bme280_init(void);
esp_err_t bme280_read(float *temp_c, float *press_hpa, float *hum_pct);
bool bme280_is_valid(void);

#ifdef __cplusplus
}
#endif