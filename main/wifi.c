#include <string.h>
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi.h"

static const char *TAG = "wifi";

#define WIFI_AP_SSID     "DispEsp32"
#define WIFI_AP_PASSWORD ""

static esp_netif_t *s_ap_netif = NULL;
static esp_netif_t *s_sta_netif = NULL;
static bool s_sta_connected = false;
static char s_active_ssid[33] = WIFI_AP_SSID;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t *)data;
        ESP_LOGI(TAG, "station joined, mac=" MACSTR, MAC2STR(ev->mac));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "station left");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        ESP_LOGW(TAG, "STA disconnected, retrying...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        s_sta_connected = true;
        ESP_LOGI(TAG, "Connected to router! IP: " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

esp_err_t wifi_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("wifi_cfg", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass ? pass : "");
    err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t wifi_init(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };

    char sta_ssid[33] = {0};
    char sta_pass[65] = {0};
    nvs_handle_t h;
    if (nvs_open("wifi_cfg", NVS_READONLY, &h) == ESP_OK) {
        size_t len_s = sizeof(sta_ssid);
        size_t len_p = sizeof(sta_pass);
        nvs_get_str(h, "ssid", sta_ssid, &len_s);
        nvs_get_str(h, "pass", sta_pass, &len_p);
        nvs_close(h);
    }

    if (sta_ssid[0] != '\0') {
        ESP_LOGI(TAG, "Connecting to saved SSID: %s", sta_ssid);
        snprintf(s_active_ssid, sizeof(s_active_ssid), "%s", sta_ssid);
        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid) - 1);
        strncpy((char *)sta_config.sta.password, sta_pass, sizeof(sta_config.sta.password) - 1);

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    } else {
        ESP_LOGI(TAG, "No saved STA credentials, starting AP only");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    }

    return esp_wifi_start();
}

esp_err_t wifi_start_ap(void)
{
    return ESP_OK;
}

bool wifi_is_connected_to_ap(void)
{
    return s_sta_connected;
}

void wifi_get_ssid(char *ssid, size_t len)
{
    if (!ssid || len == 0) return;
    snprintf(ssid, len, "%s", s_active_ssid);
}
