/*
 * wifi_ap.c — WiFi AP/STA setup with NVS credential storage for the DispEsp32 board.
 *
 * On boot:
 *   - If WiFi credentials are stored in NVS → start station mode and connect
 *   - Otherwise → start open WiFi AP with SSID "DispEsp32" (captive portal mode)
 *
 * If STA connection fails repeatedly (wrong credentials, network down), falls back
 * to AP mode so the device stays reachable.
 *
 * Credentials can be saved via the web UI at GET /wifi (form) / POST /wifi (save).
 */
#include <string.h>
#include "wifi_ap.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "wifi_ap";

#define NVS_NAMESPACE   "wifi_cfg"
#define NVS_KEY_SSID    "ssid"
#define NVS_KEY_PASS    "pass"
#define STA_MAX_RETRIES 5   /* fall back to AP after this many failed reconnect attempts */

static int s_sta_retry_count = 0;

/* Forward declaration */
static void wifi_ap_start(void);

static void wifi_ap_start(void)
{
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "DispEsp32",
            .password = "",
            .ssid_len = 9,
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,
            .ssid_hidden = 0,
            .max_connection = 4,
            .beacon_interval = 100,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started. SSID: DispEsp32 (open, no password)");
}

static void wifi_sta_start(const char *ssid, const char *pass)
{
    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi station started. Connecting to SSID: %s", ssid);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_retry_count++;
        ESP_LOGI(TAG, "STA disconnected (retry %d/%d)", s_sta_retry_count, STA_MAX_RETRIES);
        if (s_sta_retry_count >= STA_MAX_RETRIES) {
            ESP_LOGI(TAG, "Max STA retries reached. Falling back to AP mode.");
            esp_wifi_stop();
            wifi_ap_start();
        } else {
            esp_wifi_connect();
        }
    }
}

void wifi_ap_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Create both netifs upfront so we can switch between STA and AP at runtime */
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));

    /* Check NVS for stored credentials */
    nvs_handle_t nvs;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        char ssid[33] = { 0 };
        char pass[65] = { 0 };
        size_t ssid_len = sizeof(ssid);
        size_t pass_len = sizeof(pass);

        esp_err_t ssid_err = nvs_get_str(nvs, NVS_KEY_SSID, ssid, &ssid_len);
        esp_err_t pass_err = nvs_get_str(nvs, NVS_KEY_PASS, pass, &pass_len);
        nvs_close(nvs);

        if (ssid_err == ESP_OK && pass_err == ESP_OK && ssid[0] != '\0') {
            ESP_LOGI(TAG, "Stored WiFi credentials found. Starting station mode.");
            s_sta_retry_count = 0;
            wifi_sta_start(ssid, pass);
            return;
        }
    }

    /* No credentials → fall back to AP mode */
    ESP_LOGI(TAG, "No stored WiFi credentials. Starting AP mode.");
    wifi_ap_start();
}

esp_err_t wifi_save_credentials(const char *ssid, const char *pass)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }

    err = nvs_set_str(nvs, NVS_KEY_PASS, pass);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    nvs_close(nvs);
    return err;
}
