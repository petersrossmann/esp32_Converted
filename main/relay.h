#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t relay_init(void);
esp_err_t relay_set(bool on);
bool relay_get(void);

#ifdef __cplusplus
}
#endif