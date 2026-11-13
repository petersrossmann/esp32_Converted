#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the ESP32 as a WiFi access point with SSID "DispEsp32" (open, no password). */
void wifi_ap_init(void);

/* Save WiFi credentials to NVS. Returns ESP_OK on success. */
esp_err_t wifi_save_credentials(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
#endif /* WIFI_AP_H */