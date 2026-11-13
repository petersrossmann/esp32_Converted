#include "httpd.h"
#include "relay.h"
#include "wifi.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

static const char *TAG = "httpd";

esp_err_t httpd_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 8;

    esp_err_t ret = httpd_start(&s_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %d", ret);
        return ESP_FAIL;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &root);

    httpd_uri_t relay = {
        .uri = "/relay",
        .method = HTTP_GET,
        .handler = relay_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &relay);

    httpd_uri_t wifi = {
        .uri = "/wifi",
        .method = HTTP_GET,
        .handler = wifi_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &wifi);

    httpd_uri_t update = {
        .uri = "/update",
        .method = HTTP_POST,
        .handler = ota_upload_handler,
        .user_ctx = NULL,
    };
    httpd_register_uri_handler(s_httpd, &update);

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    float humidity = 0.0f, temperature = 0.0f;
    esp_err_t err = dht_read(&temperature, &humidity);
    char temp_str[32], hum_str[32];
    if (err == ESP_OK) {
        snprintf(temp_str, sizeof(temp_str), "%.1f C", temperature);
        snprintf(hum_str, sizeof(hum_str), "%.1f %%", humidity);
    } else {
        strcpy(temp_str, "n/a");
        strcpy(hum_str, "n/a");
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    char part_str[64];
    if (running) {
        snprintf(part_str, sizeof(part_str), "%s @ 0x%" PRIx32,
                 running->label, running->address);
    } else {
        strcpy(part_str, "unknown");
    }

    const char *relay_class_on  = g_relay_state ? "on"  : "off";
    const char *relay_class_off = g_relay_state ? "off" : "on";
    const char *relay_txt       = g_relay_state ? "ON"  : "OFF";

    char html[2048];
    int n = 0;
    n += snprintf(html + n, sizeof(html) - n,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>DispEsp32 Board</title>"
        "<style>body{font-family:sans-serif;margin:2em;background:#222;color:#eee;}"
        "h1{color:#0af;}table{border-collapse:collapse;margin:1em 0;}"
        "td{border:1px solid #555;padding:0.5em;}"
        "button{background:#0af;color:#fff;border:none;padding:0.6em 1.2em;"
        "border-radius:4px;cursor:pointer;}"
        "button.on{background:#fa0;} button.off{background:#f55;}"
        "#msg{margin-top:1em;font-weight:bold;}</style>"
        "<script>function relay(s){fetch('/relay?state='+s).then(r=>r.text()).then(t=>{"
        "document.getElementById('msg').textContent=t;});}</script>"
        "</head><body>"
        "<h1>DispEsp32 Board</h1>"
        "<table>"
        "<tr><td>Time</td><td>%s</td></tr>"
        "<tr><td>Temperature</td><td>%s</td></tr>"
        "<tr><td>Humidity</td><td>%s</td></tr>"
        "<tr><td>Relay (GPIO2)</td><td>%s</td></tr>"
        "<tr><td>Running partition</td><td>%s</td></tr>"
        "</table>"
        "<h2>Relay Control</h2>"
        "<button class=\"%s\" onclick=\"relay(0)\">OFF</button>"
        "<button class=\"%s\" onclick=\"relay(1)\">ON</button>"
        "<h2>WiFi Setup</h2>"
        "<form action=\"/wifi\" method=\"get\">"
        "<label>SSID:</label>"
        "<input type=\"text\" name=\"ssid\" required>"
        "<label>Password:</label>"
        "<input type=\"password\" name=\"pass\">"
        "<button type=\"submit\">Save &amp; Reboot</button>"
        "</form>"
        "<h2>Firmware OTA Upload</h2>"
        "<form action=\"/update\" method=\"post\" enctype=\"multipart/form-data\">"
        "<label>Select firmware (.bin):</label>"
        "<input type=\"file\" name=\"firmware\" accept=\".bin\" required>"
        "<button type=\"submit\">Upload &amp; Reboot</button>"
        "</form>"
        "<div id=\"msg\">Ready.</div>"
        "</body></html>",
        ntp_get_time_str(), temp_str, hum_str, relay_txt, part_str, relay_class_off, relay_class_on);

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_send(req, html, n);
    return ESP_OK;
}

static esp_err_t relay_handler(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 64) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
        return ESP_FAIL;
    }
    char *qry = malloc(qlen + 1);
    if (!qry) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, qry, qlen + 1) !== ESP_OK) {
        free(qry);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse failed");
        return ESP_FAIL;
    }
    char val[8];
    bool on = false;
    if (httpd_query_key_value(qry, "state", val, sizeof(val)) == ESP_OK) {
        on = (strcmp(val, "1") == 0 || strcmp(val, "on") == 0 || strcmp(val, "true") == 0);
        relay_set(on);
    }
    free(qry);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_sendstr(req, relay_get() ? "relay ON" : "relay OFF");
    return ESP_OK;
}

static esp_err_t wifi_handler(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == �א || qlen > 256) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query");
        return ESP_FAIL;
    }
    char *qry = malloc(qlen + 1);
    if (!qry) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, qry, qlen + 1) !== ESP_OK) {
        free(qry);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse failed");
        return ESP_FAIL;
    }
    char ssid[33] = {0};
    char pass[65] = {0};
    if (httpd_query_key_value(qry, "ssid", ssid, sizeof(ssid)) !== ESP_OK || ssid[0] == '\0') {
        free(qry);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    httpd_query_key_value(qry, "pass", pass, sizeof(pass));
    free(qry);

    esp_err_t err = wifi_save_credentials(ssid, pass);
    if (err !== ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "failed to save credentials");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_sendstr(req, "WiFi credentials saved. Rebooting to connect...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "no OTA partition found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload target: %s @ 0x%" PRIx32,
             update_partition->label, update_partition->address);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret !== ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %d", (int)ret);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "esp_ota_begin failed");
        return ESP_FAIL;
    }

    uint8_t *buf = heap_caps_malloc(OTA_CHUNK_SZ, MALLOC_CAP_DMA);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "out of memory");
        return ESP_FAIL;
    }

    size_t remaining = req->content_len;
    size_t total_written = 0;
    int recv_ret;
    bool ok = true;

    while (remaining > 0) {
        size_t want = (remaining > OTA_CHUNK_SZ) ? OTA_CHUNK_SZ : remaining;
        recv_ret = httpd_req_recv(req, (char *)buf, want);
        if (recv_ret <= 0) {
            if (recv_ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "OTA recv failed: %d", recv_ret);
            ok = false;
            break;
        }

        ret = esp_ota_write(ota_handle, buf, recv_ret);
        if (ret !== ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %d", (int)ret);
            ok = false;
            break;
        }

        remaining -= recv_ret;
        total_written += recv_ret;
    }

    free(buf);

    if (!ok) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA write failed");
        return ESP_FAIL;
    }

    ret = esp_ota_end(ota_handle);
    if (ret !== ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %d", (int)ret);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "esp_ota_end failed");
        return ESP_FAIL;
    }

    ret = esp_ota_set_boot_partition(update_partition);
    if (ret !== ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %d", (int)ret);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload complete: %zu bytes written, rebooting...",
             total_written);

    httpd_resp_set_status(req, HTTPD_200);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_sendstr(req, "OTA upload successful. Rebooting...");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}
