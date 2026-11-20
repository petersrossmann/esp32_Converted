/*
 * config.h — project configuration, carried over from the old ESP8266
 * user/c_config.h concept: hardware/feature selection via #defines.
 * Comment/uncomment to include/exclude features from the build.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

/*---------------------------------------------------------- feature toggles */
/* Comment out to disable a feature — each changes the compiled binary */

/* --- Sensors --- */
#define xSENSOR_DHT22
#define xSENSOR_DS18B20
#define SENSOR_BME280

/* --- Outputs --- */
#define RELAY_ENABLED
#define DISPLAY_ENABLED
//#define WS2812_ENABLED            /* Moodlight — no hardware on this board */

/* --- Connectivity --- */
#define WIFI_ENABLED
#define NTP_ENABLED
#define HTTPD_ENABLED
#define OTA_ENABLED
//#define EMAIL_ENABLED             /* SMTP client via ESP-TLS */

/* --- Port state / contact --- */
//#define PORTSTATE_ENABLED         /* Contact sensor instead of temperature */
#define RX_PORTSTATE_ENABLED      /* Contact on RXD */

/* --- Automation --- */
//#define THERMOSTAT_ENABLED
//#define ROLLADEN_ENABLED          /* Rolling shutter */
//#define OBI_PLUG_ENABLED          /* OBI WiFi plug variant */
//#define SWITCH_ON_CONTACT_ENABLED
//#define SWITCH_ON_CONTACT_NIGHT_ENABLED
#define TOGGLE_RELAYS_SWITCH
//#define PORTSTATE_EMAIL_ENABLED

/* --- Other --- */
#define SHUTTER_RUNTIME 40000      /* ms for rolling shutter travel time */

/*---------------------------------------------------------- board pinout */

/*---------------------------------------------------------- board pinout */
/* From DispEsp32.pdf schematic (IL9341 on connector JP4) */
#define ILI9341_MOSI_GPIO   GPIO_NUM_23
#define ILI9341_CLK_GPIO    GPIO_NUM_18
#define ILI9341_CS_GPIO     GPIO_NUM_5
#define ILI9341_DC_GPIO     GPIO_NUM_4
#define ILI9341_BL_GPIO     GPIO_NUM_19    /* backlight */
#define ILI9341_HOST        SPI2_HOST
#define ILI9341_WIDTH       320
#define ILI9341_HEIGHT      240
#define SPI_SPEED_HZ        40000000

#define DHT_DATA_GPIO       GPIO_NUM_0     /* DHT22 single-wire bus */

#define RELAY_GPIO          GPIO_NUM_2     /* active HIGH; LOW at boot */
#define DS18B20_GPIO        GPIO_NUM_0     /* DS18B20 temperature sensor */

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