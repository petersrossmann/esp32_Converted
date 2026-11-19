#include "ota_client.h"
#include "config_store.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#define OTA_SRV_SZ 128
#define OTA_URL_SZ 256
#define OTA_BUF_SZ 4096

static const char *TAG = "ota_client";
static volatile bool s_ota_running = false;

static void ota_task(void *pv)
{
    bool delayed = (pv != NULL);
    if (delayed) {
        vTaskDelay(pdMS_TO_TICKS(60000)); /* 60s nach Boot warten */
    }

    char server[OTA_SRV_SZ];
    int port = 0;
    config_store_get_all_ota(server, sizeof(server), &port);
    if (*server == 0 || port <= 0) {
        ESP_LOGI(TAG, "OTA server not configured, skipping");
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    char url[OTA_URL_SZ];
    if (port == 443) {
        snprintf(url, sizeof(url), "https://%s/firmware.bin", server);
    } else {
        snprintf(url, sizeof(url), "http://%s:%d/firmware.bin", server, port);
    }
    ESP_LOGI(TAG, "OTA checking %s", url);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGI(TAG, "OTA server unreachable");
        esp_http_client_cleanup(client);
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    int content_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200 || content_len <= 0) {
        ESP_LOGI(TAG, "No firmware update available (HTTP %d)", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    uint8_t *buf = malloc(OTA_BUF_SZ);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    int first_read = esp_http_client_read(client, (char *)buf, OTA_BUF_SZ);
    size_t hdr_min = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);
    if (first_read < (int)hdr_min) {
        ESP_LOGE(TAG, "Firmware header too short");
        free(buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    esp_app_desc_t new_app_info;
    memcpy(&new_app_info, buf + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t), sizeof(esp_app_desc_t));
    const esp_app_desc_t *running_app_info = esp_app_get_description();

    if (strcmp(new_app_info.version, running_app_info->version) == 0) {
        ESP_LOGI(TAG, "Firmware already up to date (version %s), skipping OTA", running_app_info->version);
        free(buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "New firmware found: %s (current: %s) -> starting OTA!", new_app_info.version, running_app_info->version);

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t ota_handle;
    if (!update_partition || esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed");
        free(buf);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = esp_ota_write(ota_handle, buf, first_read);
    size_t total = first_read;
    bool ok = (err == ESP_OK);

    while (ok) {
        int r = esp_http_client_read(client, (char *)buf, OTA_BUF_SZ);
        if (r < 0) { ok = false; break; }
        if (r == 0) break;
        if (esp_ota_write(ota_handle, buf, r) != ESP_OK) { ok = false; break; }
        total += r;
    }

    free(buf);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (!ok || esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(update_partition) != ESP_OK) {
        esp_ota_abort(ota_handle);
        ESP_LOGE(TAG, "OTA failed after %zu bytes", total);
        s_ota_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "OTA success (%s, %zu bytes), rebooting...", new_app_info.version, total);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void ota_client_start(void)
{
    if (s_ota_running) return;
    s_ota_running = true;
    xTaskCreate(ota_task, "ota_client", 8192, (void *)1, 2, NULL);
}

void ota_client_check_now(void)
{
    if (s_ota_running) return;
    s_ota_running = true;
    xTaskCreate(ota_task, "ota_now", 8192, NULL, 2, NULL);
}
