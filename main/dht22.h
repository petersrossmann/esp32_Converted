#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t dht_init(void);
esp_err_t dht_read(float *temperature, float *humidity);

#ifdef __cplusplus
}
#endif