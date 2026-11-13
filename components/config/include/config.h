/*
 * config.h — project configuration, carried over from the old ESP8266
 * user/c_config.h concept: hardware/feature selection via #defines.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#define SW_VERSION "1.0"

/*---------------------------------------------------------- board pinout */
/* From DispEsp32.pdf schematic (IL9341 on connector JP4) */
#define ILI9341_MOSI_GPIO   GPIO_NUM_23
#define ILI9341_CLK_GPIO    GPIO_NUM_18
#define ILI9341_CS_GPIO     GPIO_NUM_5
#define ILI9341_DC_GPIO     GPIO_NUM_4
#define ILI9341_BL_GPIO     GPIO_NUM_19    /* backlight */
#define ILI9341_HOST        SPI2_HOST
#define ILI9341_WIDTH       240
#define ILI9341_HEIGHT      320
#define SPI_SPEED_HZ        10000000

#define DHT_DATA_GPIO       GPIO_NUM_0     /* DHT22 single-wire bus */

#define RELAY_GPIO          GPIO_NUM_2     /* active HIGH; LOW at boot */

/*---------------------------------------------------------- behaviour */
#define RELAY_DEFAULT       0              /* relay state after power-on */
#define DISPLAY_TIMEOUT_MIN 2              /* backlight off after N minutes */
#define DISPLAY_ACTIVITY_START_HOUR 8
#define DISPLAY_ACTIVITY_START_MIN  0
#define DISPLAY_ACTIVITY_END_HOUR   22
#define DISPLAY_ACTIVITY_END_MIN    0

/*---------------------------------------------------------- debug */
#define DBG_NONE        0
#define DBG_CGI         (1 << 2)
#define DBG_NTP         (1 << 3)
#define DBG_HTTPD       (1 << 4)
#define DBG_RBOOT       (1 << 5)
#define DBG_CONFIG      (1 << 6)
#define DBG_IO          (1 << 7)
#define DBG_DS18B20     (1 << 8)
#define DBG_DHT22       (1 << 9)
#define DBG_ESPFS       (1 << 11)
#define DBG_THERMOSTAT  (1 << 14)

#define DEBUG_LEVEL     DBG_NONE
