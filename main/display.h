#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ILI9341_ROTATION_PORTRAIT_1 = 0,
    ILI9341_ROTATION_PORTRAIT_2 = 1,
    ILI9341_ROTATION_LANDSCAPE_1 = 2,
    ILI9341_ROTATION_LANDSCAPE_2 = 3,
} ili9341_rotation_t;

esp_err_t display_init(void);
esp_err_t display_text(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint16_t bg);
esp_err_t display_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
esp_err_t display_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);
esp_err_t display_filled_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t color);
esp_err_t display_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
esp_err_t display_filled_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
esp_err_t display_set_rotation(ili9341_rotation_t rotation);

/* HTTP-driven command string (T[...] / L[...] / R[...] / O[...] etc.) */
esp_err_t display_execute_command(const char *cmd);
void display_set(const char *cmd);
const char *display_get(void);

/* Render sensor readings on the display */
void display_show_readings(const char *time_str, float temp, float humid, bool relay_state);

#ifdef __cplusplus
}
#endif