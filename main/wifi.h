#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_init(void);
esp_err_t wifi_start_ap(void);
esp_err_t wifi_save_credentials(const char *ssid, const char *pass);
bool wifi_is_connected_to_ap(void);
void wifi_get_ssid(char *ssid, size_t len);

#ifdef __cplusplus
}
#endif