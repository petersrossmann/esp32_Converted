#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_init(void);
esp_err_t wifi_start_ap(void);
bool wifi_is_connected_to_ap(void);
void wifi_get_ssid(char *ssid, size_t len);

#ifdef __cplusplus
}
#endif