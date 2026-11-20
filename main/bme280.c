/*
 * bme280.c — BME280 temperature/pressure/humidity sensor via I2C.
 *
 * Adapted from the old ESP8266 user/bme280.c (Cosmin Plasoianu, GPL 2).
 * Uses ESP-IDF driver/i2c.h (I2C master) instead of bit-banged GPIO.
 */
#include <string.h>
#include "bme280.h"
#include "config_store.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "config.h"

static const char *TAG = "bme280";

#define I2C_PORT  I2C_NUM_0
#define I2C_ADDR  BME280_I2C_ADDR

/* Default pins (standard ESP32 I2C pins, override in config.h) */
#ifndef I2C_MASTER_SDA_GPIO
#define I2C_MASTER_SDA_GPIO  GPIO_NUM_21
#endif
#ifndef I2C_MASTER_SCL_GPIO
#define I2C_MASTER_SCL_GPIO  GPIO_NUM_22
#endif

#define ID_REG          0xD0
#define PRESS_MSB_REG   0xF7
#define CALIB_00_REG    0x88
#define CALIB_26_REG    0xE1
#define CONFIG_REG      0xF5
#define CTRL_MEAS_REG   0xF4
#define STATUS_REG      0xF3
#define CTRL_HUM_REG    0xF2

static struct {
    uint16_t dig_T1, dig_T2, dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4, dig_H5;
    int8_t   dig_H6;
} CalibParam;

static int32_t  temperature;
static uint32_t pressure;
static uint32_t humidity;
static int32_t  t_fine;
static bool     s_valid = false;

/*---------------------------------------------------------- I2C helpers */
static esp_err_t i2c_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_PORT, I2C_ADDR, &reg, 1, data, len,
                                         pdMS_TO_TICKS(100));
}

static esp_err_t i2c_write_reg(uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t *buf = malloc(len + 1);
    if (!buf) return ESP_ERR_NO_MEM;
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    esp_err_t ret = i2c_master_write_to_device(I2C_PORT, I2C_ADDR, buf, len + 1,
                                               pdMS_TO_TICKS(100));
    free(buf);
    return ret;
}

/*---------------------------------------------------------- compensation (ported from original) */
static int32_t compensate_t(int32_t adc_T)
{
    int32_t var1, var2, T;
    var1 = ((((adc_T >> 3) - ((int32_t)CalibParam.dig_T1 << 1))) * ((int32_t)CalibParam.dig_T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((int32_t)CalibParam.dig_T1)) * ((adc_T >> 4) - ((int32_t)CalibParam.dig_T1))) >> 12) * ((int32_t)CalibParam.dig_T3)) >> 14;
    t_fine = var1 + var2;
    T = (t_fine * 5 + 128) >> 8;
    return T;
}

static uint32_t compensate_p(int32_t adc_P)
{
    int64_t var1, var2, p;
    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)CalibParam.dig_P6;
    var2 = var2 + ((var1 * (int64_t)CalibParam.dig_P5) << 17);
    var2 = var2 + (((int64_t)CalibParam.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)CalibParam.dig_P3) >> 8) + ((var1 * (int64_t)CalibParam.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)CalibParam.dig_P1) >> 33;
    if (var1 == 0) return 0;
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)CalibParam.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)CalibParam.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)CalibParam.dig_P7) << 4);
    /* sea-level correction (simplified barometric formula) */
    p = p + ((((SEALEVEL * 100) / 830) * 100) << 8);
    return (uint32_t)p;
}

static uint32_t compensate_h(int32_t adc_H)
{
    int32_t v_x1_u32r;
    v_x1_u32r = (t_fine - ((int32_t)76800));
    v_x1_u32r = (((((adc_H << 14) - (((int32_t)CalibParam.dig_H4) << 20) - (((int32_t)CalibParam.dig_H5) * v_x1_u32r)) + ((int32_t)16384)) >> 15) * (((((((v_x1_u32r * ((int32_t)CalibParam.dig_H6)) >> 10) * (((v_x1_u32r * ((int32_t)CalibParam.dig_H3)) >> 11) + ((int32_t)32768))) >> 10) + ((int32_t)2097152)) * ((int32_t)CalibParam.dig_H2) + 8192) >> 14));
    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) * ((int32_t)CalibParam.dig_H1)) >> 4));
    v_x1_u32r = (v_x1_u32r < 0 ? 0 : v_x1_u32r);
    v_x1_u32r = (v_x1_u32r > 419430400 ? 419430400 : v_x1_u32r);
    return (uint32_t)(v_x1_u32r >> 12);
}

/*---------------------------------------------------------- init */
esp_err_t bme280_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_GPIO,
        .scl_io_num = I2C_MASTER_SCL_GPIO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
#if 0
    /* --- Quick I2C Scanner --- */
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d, SCL=%d)...", I2C_MASTER_SDA_GPIO, I2C_MASTER_SCL_GPIO);
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C device found at 0x%02X", addr);
        }
    }
#endif
    uint8_t id = 0;
    if (i2c_read_reg(ID_REG, &id, 1) != ESP_OK || id != 0x60) {
        ESP_LOGE(TAG, "BME280 not found (ID=0x%02x)", id);
        return ESP_ERR_NOT_FOUND;
    }

    /* Read calibration data */
    uint8_t buf[26];
    i2c_read_reg(CALIB_00_REG, buf, 26);
    CalibParam.dig_T1 = (buf[1] << 8) | buf[0];
    CalibParam.dig_T2 = (buf[3] << 8) | buf[2];
    CalibParam.dig_T3 = (buf[5] << 8) | buf[4];
    CalibParam.dig_P1 = (buf[7] << 8) | buf[6];
    CalibParam.dig_P2 = (buf[9] << 8) | buf[8];
    CalibParam.dig_P3 = (buf[11] << 8) | buf[10];
    CalibParam.dig_P4 = (buf[13] << 8) | buf[12];
    CalibParam.dig_P5 = (buf[15] << 8) | buf[14];
    CalibParam.dig_P6 = (buf[17] << 8) | buf[16];
    CalibParam.dig_P7 = (buf[19] << 8) | buf[18];
    CalibParam.dig_P8 = (buf[21] << 8) | buf[20];
    CalibParam.dig_P9 = (buf[23] << 8) | buf[22];
    CalibParam.dig_H1 = buf[25];
    memset(buf, 0, 7);
    i2c_read_reg(CALIB_26_REG, buf, 7);
    CalibParam.dig_H2 = (buf[1] << 8) | buf[0];
    CalibParam.dig_H3 = buf[2];
    CalibParam.dig_H4 = (buf[3] << 4) | (buf[4] & 0x0F);
    CalibParam.dig_H5 = (buf[5] << 4) | (buf[4] >> 4);
    CalibParam.dig_H6 = buf[6];

    /* Configure: normal mode, 16x oversampling, 0.5ms standby */
    uint8_t tmp = (BME280_TSB_05 << 5) | (BME280_FILTER_16 << 2);
    i2c_write_reg(CONFIG_REG, &tmp, 1);
    tmp = BME280_OS_H_16;
    i2c_write_reg(CTRL_HUM_REG, &tmp, 1);
    tmp = (BME280_OS_T_16 << 5) | (BME280_OS_P_16 << 2) | BME280_MODE_NORMAL;
    i2c_write_reg(CTRL_MEAS_REG, &tmp, 1);

    ESP_LOGI(TAG, "BME280 initialized");
    return ESP_OK;
}

esp_err_t bme280_read(float *temp_c, float *press_hpa, float *hum_pct)
{
    uint8_t buf[8];
    if (i2c_read_reg(PRESS_MSB_REG, buf, 8) != ESP_OK) {
        return ESP_FAIL;
    }

    int32_t adc_p = ((uint32_t)buf[0] << 16) | ((uint16_t)buf[1] << 8) | buf[2];
    adc_p >>= 4;
    int32_t adc_t = ((uint32_t)buf[3] << 16) | ((uint16_t)buf[4] << 8) | buf[5];
    adc_t >>= 4;
    int32_t adc_h = ((uint16_t)buf[6] << 8) | buf[7];

    int32_t t_raw = compensate_t(adc_t);
    uint32_t p_raw = compensate_p(adc_p);
    uint32_t h_raw = compensate_h(adc_h);

    temperature = t_raw;
    pressure = p_raw;
    humidity = h_raw;
    s_valid = true;

    if (temp_c)    *temp_c    = t_raw / 100.0f + config_store_get_temperature_offset() / 10.0f;
    if (press_hpa) *press_hpa = (p_raw >> 8) / 100.0f;
//    if (hum_pct)   *hum_pct   = (h_raw >> 12) / 100.0f + config_store_get_humidity_offset() / 10.0f;
    if (hum_pct) *hum_pct = (h_raw / 1024.0f) + config_store_get_humidity_offset() / 10.0f;  /* Bosch formula */


    return ESP_OK;
}

bool bme280_is_valid(void) { return s_valid; }