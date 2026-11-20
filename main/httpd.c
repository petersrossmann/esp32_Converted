/*
 * httpd.c — HTTP server for the DispEsp32 board.
 */
#include "config.h"
#include "httpd.h"
#include "relay.h"
#include "wifi.h"
#include "ntp.h"
#include "dht22.h"
#include "bme280.h"
#include "config_store.h"
#include "display.h"
#include "portstate.h"
#include "tpl_handler.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#ifdef OTA_ENABLED
#include "ota_client.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_heap_caps.h"
#include "smtp_client.h"
#endif
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <inttypes.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_wifi.h"

static const char *TAG = "httpd";

extern const char config_tpl_start[]       asm("_binary_config_tpl_start");
extern const char timeschedule_tpl_start[] asm("_binary_timeschedule_tpl_start");
extern const char duskdawn_tpl_start[]     asm("_binary_duskdawn_tpl_start");
extern const char display_tpl_start[]      asm("_binary_display_tpl_start");
extern const char debug_tpl_start[]        asm("_binary_debug_tpl_start");
extern const char index_html_start[]       asm("_binary_index_html_start");
extern const uint8_t favicon_ico_start[] asm("_binary_favicon_ico_start");
extern const uint8_t favicon_ico_end[]   asm("_binary_favicon_ico_end");

static bool exact_match(const char *ref, const char *uri, size_t len)
{
    /* Query-String (?...) bei der Längenprüfung ignorieren */
    const char *q = memchr(uri, '?', len);
    size_t path_len = q ? (size_t)(q - uri) : len;
    return (strlen(ref) == path_len) && (strncmp(ref, uri, path_len) == 0);
}

static void url_decode(char *dst, const char *src, size_t dst_size)
{
    size_t i = 0;
    while (*src && i + 1 < dst_size) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], 0 };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            dst[i++] = ' ';
            src++;
        } else {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

#ifdef RELAY_ENABLED
extern bool g_relay_state;
#endif

#ifdef SENSOR_DS18B20
extern float g_ds18b20_temperature;
extern bool g_ds18b20_valid;
#endif

#define OTA_CHUNK_SZ 1024
static httpd_handle_t s_httpd = NULL;
static esp_err_t testmail_handler(httpd_req_t *req);
static esp_err_t root_handler(httpd_req_t *req);
#ifdef RELAY_ENABLED
static esp_err_t relay_handler(httpd_req_t *req);
#endif
static esp_err_t wifi_handler(httpd_req_t *req);
#ifdef OTA_ENABLED
static esp_err_t ota_upload_handler(httpd_req_t *req);
#endif
static esp_err_t config_handler(httpd_req_t *req);
static esp_err_t config_set_handler(httpd_req_t *req);
static esp_err_t display_cgi_handler(httpd_req_t *req);
static esp_err_t sched_handler(httpd_req_t *req);
static esp_err_t sched_set_handler(httpd_req_t *req);
static esp_err_t ota_check_handler(httpd_req_t *req);
static esp_err_t favicon_handler(httpd_req_t *req);

esp_err_t httpd_init(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 24;
    config.uri_match_fn = exact_match;

    esp_err_t ret = httpd_start(&s_httpd, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %d", ret);
        return ESP_FAIL;
    }

#ifdef RELAY_ENABLED
    httpd_uri_t relay = { .uri = "/relay", .method = HTTP_GET, .handler = relay_handler };
    httpd_register_uri_handler(s_httpd, &relay);
#endif

    {
        httpd_uri_t wifi_get = { .uri = "/wifi", .method = HTTP_GET, .handler = wifi_handler };
        esp_err_t r = httpd_register_uri_handler(s_httpd, &wifi_get);
        ESP_LOGI(TAG, "Register /wifi GET: %s", esp_err_to_name(r));

        httpd_uri_t wifi_post = { .uri = "/wifi", .method = HTTP_POST, .handler = wifi_handler };
        httpd_register_uri_handler(s_httpd, &wifi_post);
    }

#ifdef OTA_ENABLED
    httpd_uri_t update = { .uri = "/update", .method = HTTP_POST, .handler = ota_upload_handler };
    httpd_register_uri_handler(s_httpd, &update);
#endif

    { httpd_uri_t u = { .uri = "/config", .method = HTTP_GET, .handler = config_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/config", .method = HTTP_POST, .handler = config_set_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/config.tpl", .method = HTTP_GET, .handler = tpl_config_get }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/config.cgi", .method = HTTP_GET, .handler = tpl_config_cgi }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/timeschedule.tpl", .method = HTTP_GET, .handler = tpl_timesched_get }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/timeschedule.cgi", .method = HTTP_POST, .handler = tpl_timesched_cgi }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/duskdawn.tpl", .method = HTTP_GET, .handler = tpl_duskdawn_get }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/duskdawn.cgi", .method = HTTP_POST, .handler = tpl_duskdawn_cgi }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/display.tpl", .method = HTTP_GET, .handler = tpl_display_get }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/debug.tpl", .method = HTTP_GET, .handler = tpl_debug_get }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/display.cgi", .method = HTTP_GET, .handler = display_cgi_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/sched", .method = HTTP_GET, .handler = sched_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/sched", .method = HTTP_POST, .handler = sched_set_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/", .method = HTTP_GET, .handler = root_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/testmail", .method = HTTP_GET, .handler = testmail_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/ota_check", .method = HTTP_GET, .handler = ota_check_handler }; httpd_register_uri_handler(s_httpd, &u); }
    { httpd_uri_t u = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_handler }; httpd_register_uri_handler(s_httpd, &u); }

    ESP_LOGI(TAG, "HTTP server started on port %d", config.server_port);
    return ESP_OK;
}

static esp_err_t root_handler(httpd_req_t *req)
{
    static unsigned long count = 0;
    count++;

    wifi_ap_record_t ap;
    int wifi_rssi = esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : -127;
#if 0
    FILE *f = fopen("/spiffs/index.html", "r");
    if (!f) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no index.html"); return ESP_FAIL; }

    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_len <= 0 || file_len > 4096) { fclose(f); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "bad file"); return ESP_FAIL; }

    char *html = malloc(file_len + 1);
    if (!html) { fclose(f); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    fread(html, 1, file_len, f);
    html[file_len] = 0;
    fclose(f);
#else
    const char *html = index_html_start;
    size_t file_len = strlen(html);
#endif
    float humidity = 0.0f, temperature = 0.0f; 
    float pressure = 0.0f;
    char temp_str[32] = "n/a";
    char hum_str[32]  = "n/a";
    char press_str[32] = "n/a";

#ifdef SENSOR_DHT22
//    float humidity = 0.0f, temperature = 0.0f;
    if (dht_read(&temperature, &humidity) == ESP_OK) {
        snprintf(temp_str, sizeof(temp_str), "%.1f &deg;C", temperature);
        snprintf(hum_str, sizeof(hum_str), "%.1f %%", humidity);
    }
#endif

#ifdef SENSOR_DS18B20
    if (g_ds18b20_valid) {
        snprintf(temp_str, sizeof(temp_str), "%.2f &deg;C", g_ds18b20_temperature);
    }
#endif

#ifdef SENSOR_BME280
    if (bme280_read(&temperature, &pressure, &humidity) == ESP_OK) {
        snprintf(temp_str, sizeof(temp_str), "%.1f &deg;C", temperature);
        snprintf(hum_str, sizeof(hum_str), "%.1f %%", humidity);
        snprintf(press_str, sizeof(press_str), "%.1f hPa", pressure);
    }
#endif

#ifdef OTA_ENABLED
    const esp_partition_t *running = esp_ota_get_running_partition();
    char part_str[64];
    if (running) snprintf(part_str, sizeof(part_str), "%s @ 0x%" PRIx32, running->label, running->address);
    else strcpy(part_str, "unknown");
#else
    const char *part_str = "disabled";
#endif

#ifdef RELAY_ENABLED
    const char *relay_txt = g_relay_state ? "ON" : "OFF";
    const char *relay_off_class = g_relay_state ? "off" : "on";
    const char *relay_on_class  = g_relay_state ? "on" : "off";
#else
    const char *relay_txt = "N/A";
    const char *relay_off_class = "off";
    const char *relay_on_class  = "on";
#endif

#ifdef NTP_ENABLED
    const char *time_str = ntp_get_time_str();
#else
    const char *time_str = "--:--:--";
#endif

    char deploy_str[64] = "n/a";
    config_store_get_deployment(deploy_str, sizeof(deploy_str));

#if defined PORTSTATE_ENABLED || defined RX_PORTSTATE_ENABLED
    const char *contact_str = portstate_read() ? "Open" : "Closed";
#else
    const char *contact_str = "N/A";
#endif

    char *out_buf = malloc(file_len + 512);
    if (!out_buf) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }

    const char *p = html;
    char *out = out_buf;
    const char *found;
    while ((found = strstr(p, "__"))) {
        size_t prefix_len = found - p;
        memcpy(out, p, prefix_len); out += prefix_len;
        p = found;

        if (strncmp(p, "__TIME__", 8) == 0)        { out += snprintf(out, 32, "%s", time_str); p += 8; }
        else if (strncmp(p, "__TEMP__", 8) == 0)   { out += snprintf(out, 32, "%s", temp_str); p += 8; }
        else if (strncmp(p, "__PRESS__", 9) == 0)  { out += snprintf(out, 32, "%s", press_str); p += 9; }        
        else if (strncmp(p, "__HUM__", 7) == 0)    { out += snprintf(out, 32, "%s", hum_str); p += 7; }
        else if (strncmp(p, "__RELAY__", 9) == 0)  { out += snprintf(out, 32, "%s", relay_txt); p += 9; }
        else if (strncmp(p, "__PART__", 8) == 0)   { out += snprintf(out, 64, "%s", part_str); p += 8; }
        else if (strncmp(p, "__ROFF__", 8) == 0)   { out += snprintf(out, 8, "%s", relay_off_class); p += 8; }
        else if (strncmp(p, "__RON__", 7) == 0)    { out += snprintf(out, 8, "%s", relay_on_class); p += 7; }
        else if (strncmp(p, "__COUNT__", 9) == 0)  { out += snprintf(out, 16, "%lu", (unsigned long)count); p += 9; }
        else if (strncmp(p, "__VER__", 7) == 0)    { out += snprintf(out, 32, "%s", esp_app_get_description()->version); p += 7; }
        else if (strncmp(p, "__RSSI__", 8) == 0)   { out += snprintf(out, 16, "%d", wifi_rssi); p += 8; }
        else if (strncmp(p, "__DEPLOY__", 10) == 0){ out += snprintf(out, sizeof(deploy_str), "%s", deploy_str); p += 10; }
        else if (strncmp(p, "__CONTACT__", 11) == 0){ out += snprintf(out, 16, "%s", contact_str); p += 11; }
        else { *out++ = *p++; }
    }
    size_t rest = strlen(p);
    memcpy(out, p, rest); out += rest;
    *out = 0;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, out_buf, out - out_buf);
    free(out_buf);
    return ESP_OK;
}

static esp_err_t favicon_handler(httpd_req_t *req)
{
    const size_t favicon_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
    return httpd_resp_send(req, (const char *)favicon_ico_start, favicon_size);
}


#ifdef RELAY_ENABLED
static esp_err_t relay_handler(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 64) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query"); return ESP_FAIL; }
    char *qry = malloc(qlen + 1);
    if (!qry) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, qry, qlen + 1) != ESP_OK) { free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse failed"); return ESP_FAIL; }
    char val[8];
    if (httpd_query_key_value(qry, "state", val, sizeof(val)) == ESP_OK) {
        bool on = (strcmp(val, "1") == 0 || strcmp(val, "on") == 0 || strcmp(val, "true") == 0);
        relay_set(on);
    }
    free(qry);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_sendstr(req, g_relay_state ? "relay ON" : "relay OFF");
    return ESP_OK;
}
#endif

static esp_err_t wifi_handler(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 256) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query"); return ESP_FAIL; }
    char *qry = malloc(qlen + 1);
    if (!qry) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, qry, qlen + 1) != ESP_OK) { free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse failed"); return ESP_FAIL; }

    char raw_ssid[64] = {0}, raw_pass[128] = {0};
    char ssid[33] = {0}, pass[65] = {0};
    if (httpd_query_key_value(qry, "ssid", raw_ssid, sizeof(raw_ssid)) != ESP_OK || raw_ssid[0] == '\0') {
        free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid"); return ESP_FAIL;
    }
    httpd_query_key_value(qry, "pass", raw_pass, sizeof(raw_pass));
    free(qry);

    url_decode(ssid, raw_ssid, sizeof(ssid));
    url_decode(pass, raw_pass, sizeof(pass));

    ESP_LOGI(TAG, "Saving WiFi credentials (ssid=%s)", ssid);
    esp_err_t err = wifi_save_credentials(ssid, pass);
    ESP_LOGI(TAG, "NVS save result: %s", esp_err_to_name(err));

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_sendstr(req, "WiFi credentials saved. Rebooting to connect...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

#ifdef OTA_ENABLED
static esp_err_t ota_upload_handler(httpd_req_t *req)
{
    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no OTA partition found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA upload target: %s @ 0x%" PRIx32, update_partition->label, update_partition->address);

    esp_ota_handle_t ota_handle = 0;
    esp_err_t ret = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
        return ESP_FAIL;
    }

    uint8_t *buf = heap_caps_malloc(OTA_CHUNK_SZ, MALLOC_CAP_DMA);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    size_t remaining = req->content_len;
    size_t total_written = 0;
    bool ok = true;

    while (remaining > 0) {
        size_t want = (remaining > OTA_CHUNK_SZ) ? OTA_CHUNK_SZ : remaining;
        int recv_ret = httpd_req_recv(req, (char *)buf, want);
        if (recv_ret <= 0) {
            if (recv_ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ok = false;
            break;
        }
        ret = esp_ota_write(ota_handle, buf, recv_ret);
        if (ret != ESP_OK) { ok = false; break; }
        remaining -= recv_ret;
        total_written += recv_ret;
    }

    free(buf);
    if (!ok) { esp_ota_abort(ota_handle); httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed"); return ESP_FAIL; }
    if (esp_ota_end(ota_handle) != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_end failed"); return ESP_FAIL; }
    if (esp_ota_set_boot_partition(update_partition) != ESP_OK) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "set boot partition failed"); return ESP_FAIL; }

    ESP_LOGI(TAG, "OTA upload complete: %zu bytes written, rebooting...", total_written);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    httpd_resp_sendstr(req, "OTA upload successful. Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}
#endif

static void httpd_url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)*(src + 1)) && isxdigit((unsigned char)*(src + 2))) {
            char hex[3] = { *(src + 1), *(src + 2), '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static esp_err_t display_cgi_handler(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen > 0 && qlen < 512) {
        char *qry = malloc(qlen + 1);
        if (qry) {
            if (httpd_req_get_url_query_str(req, qry, qlen + 1) == ESP_OK) {
                char cmd[256];
                if (httpd_query_key_value(qry, "cmd", cmd, sizeof(cmd)) == ESP_OK) {
                    httpd_url_decode(cmd);
                    display_set(cmd);
                }
            }
            free(qry);
        }
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/display.tpl");
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    return httpd_resp_sendstr(req, "Redirecting...");
}


static esp_err_t sched_handler(httpd_req_t *req)
{
    TimeListElt *tl = ntp_get_timelist();
    char buf[2048];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "Time Scheduler Table\n\n");
    for (int i = 0; i < 16; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "[%d] status=0x%04x runtime=%d on=%02d:%02d off=%02d:%02d\n",
                      i, tl[i].status, tl[i].runtime, tl[i].hour_on, tl[i].minute_on, tl[i].hour_off, tl[i].minute_off);
    }
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t sched_set_handler(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 256) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query"); return ESP_FAIL; }
    char *qry = malloc(qlen + 1);
    if (!qry) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, qry, qlen + 1) != ESP_OK) { free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse"); return ESP_FAIL; }

    char val[16];
    int idx = -1;
    TimeListElt *tl = ntp_get_timelist();
    if (httpd_query_key_value(qry, "idx", val, sizeof(val)) == ESP_OK) idx = atoi(val);
    if (idx < 0 || idx >= 16) { free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad idx"); return ESP_FAIL; }

    if (httpd_query_key_value(qry, "status", val, sizeof(val)) == ESP_OK) tl[idx].status = (uint16_t)strtol(val, NULL, 0);
    if (httpd_query_key_value(qry, "runtime", val, sizeof(val)) == ESP_OK) tl[idx].runtime = (uint8_t)atoi(val);
    if (httpd_query_key_value(qry, "on_h", val, sizeof(val)) == ESP_OK) tl[idx].hour_on = (uint8_t)atoi(val);
    if (httpd_query_key_value(qry, "on_m", val, sizeof(val)) == ESP_OK) tl[idx].minute_on = (uint8_t)atoi(val);
    if (httpd_query_key_value(qry, "off_h", val, sizeof(val)) == ESP_OK) tl[idx].hour_off = (uint8_t)atoi(val);
    if (httpd_query_key_value(qry, "off_m", val, sizeof(val)) == ESP_OK) tl[idx].minute_off = (uint8_t)atoi(val);

    free(qry);
    ntp_save_timelist();
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t config_handler(httpd_req_t *req)
{
    char buf[1024];
    int n = 0;
    n += snprintf(buf + n, sizeof(buf) - n, "Config (from NVS / compile-time defaults)\n\n");

    char s[64];
    config_store_get_ntp_server(s, sizeof(s));
    n += snprintf(buf + n, sizeof(buf) - n, "ntp_server=%s\n", s);
    n += snprintf(buf + n, sizeof(buf) - n, "ntp_timezone=%d\n", config_store_get_ntp_timezone());
    n += snprintf(buf + n, sizeof(buf) - n, "temp_offset=%d\n", (int)config_store_get_temperature_offset());
    n += snprintf(buf + n, sizeof(buf) - n, "hum_offset=%d\n", (int)config_store_get_humidity_offset());
    n += snprintf(buf + n, sizeof(buf) - n, "debug_level=%d\n", (int)config_store_get_debug_level());
    n += snprintf(buf + n, sizeof(buf) - n, "display_timeout=%d\n", (int)config_store_get_display_timeout());
    {
        uint8_t sh, sm, eh, em;
        config_store_get_display_activity(&sh, &sm, &eh, &em);
        n += snprintf(buf + n, sizeof(buf) - n, "disp_act_start=%02d:%02d\n", sh, sm);
    }
    config_store_get_deployment(s, sizeof(s)); // <-- HIER EINFÜGEN!
    n += snprintf(buf + n, sizeof(buf) - n, "deploy=%s\n", s);
    n += snprintf(buf + n, sizeof(buf) - n, "lat=%g\n", config_store_get_latitude());
    n += snprintf(buf + n, sizeof(buf) - n, "lon=%g\n", config_store_get_longitude());
    config_store_get_smtp_server(s, sizeof(s));
    n += snprintf(buf + n, sizeof(buf) - n, "smtp_server=%s\n", s);
    n += snprintf(buf + n, sizeof(buf) - n, "smtp_port=%d\n", config_store_get_smtp_port());
    config_store_get_smtp_sender(s, sizeof(s));
    n += snprintf(buf + n, sizeof(buf) - n, "smtp_sender=%s\n", s);
    config_store_get_smtp_recipient(s, sizeof(s));
    n += snprintf(buf + n, sizeof(buf) - n, "smtp_recipient=%s\n", s);
    n += snprintf(buf + n, sizeof(buf) - n, "smtp_pass=****\n");

    config_store_get_ota_server(s, sizeof(s));
    n += snprintf(buf + n, sizeof(buf) - n, "ota_server=%s\n", s);
    n += snprintf(buf + n, sizeof(buf) - n, "ota_port=%d\n", config_store_get_ota_port());
    n += snprintf(buf + n, sizeof(buf) - n, "cfg_status=0x%lx\n", config_store_get_status());

    for (int i = 0; i < 8; i++) {
        int16_t on_off, off_off;
        config_store_get_duskdawn(i, &on_off, &off_off);
        n += snprintf(buf + n, sizeof(buf) - n, "dd_%d_on=%d\n", i, on_off);
        n += snprintf(buf + n, sizeof(buf) - n, "dd_%d_off=%d\n", i, off_off);
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

static esp_err_t config_set_handler(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 256) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing query"); return ESP_FAIL; }
    char *qry = malloc(qlen + 1);
    if (!qry) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    if (httpd_req_get_url_query_str(req, qry, qlen + 1) != ESP_OK) { free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "parse"); return ESP_FAIL; }

    char key[32], val[64];
    if (httpd_query_key_value(qry, "k", key, sizeof(key)) != ESP_OK) { free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing k"); return ESP_FAIL; }
    if (httpd_query_key_value(qry, "v", val, sizeof(val)) != ESP_OK) { free(qry); httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing v"); return ESP_FAIL; }
    free(qry);

    if (strcmp(key, "ntp_server") == 0) config_store_set_ntp_server(val);
    else if (strcmp(key, "ntp_timezone") == 0) config_store_set_ntp_timezone(atoi(val));
    else if (strcmp(key, "temp_offset") == 0) config_store_set_temperature_offset(atol(val));
    else if (strcmp(key, "hum_offset") == 0) config_store_set_humidity_offset(atol(val));
    else if (strcmp(key, "debug_level") == 0) config_store_set_debug_level(atol(val));
    else if (strcmp(key, "ts_h") == 0 || strcmp(key, "ts_m") == 0 ||
             strcmp(key, "te_h") == 0 || strcmp(key, "te_m") == 0) {
        uint8_t sh,sm,eh,em;
        config_store_get_display_activity(&sh, &sm, &eh, &em);
        if (strcmp(key, "ts_h") == 0) sh = atoi(val);
        if (strcmp(key, "ts_m") == 0) sm = atoi(val);
        if (strcmp(key, "te_h") == 0) eh = atoi(val);
        if (strcmp(key, "te_m") == 0) em = atoi(val);
        config_store_set_display_activity(sh, sm, eh, em);
    } else if (strcmp(key, "deploy") == 0) config_store_set_deployment(val);
    else if (strcmp(key, "lat") == 0) config_store_set_latitude(atof(val));
    else if (strcmp(key, "lon") == 0) config_store_set_longitude(atof(val));
    else if (strcmp(key, "smtp_server") == 0) config_store_set_smtp_server(val);
    else if (strcmp(key, "smtp_port") == 0) config_store_set_smtp_port(atoi(val));
    else if (strcmp(key, "smtp_sender") == 0) config_store_set_smtp_sender(val);
    else if (strcmp(key, "smtp_recipient") == 0) config_store_set_smtp_recipient(val);
    else if (strcmp(key, "smtp_pass") == 0) config_store_set_smtp_password(val);
    else if (strcmp(key, "ota_server") == 0) config_store_set_ota_server(val);
    else if (strcmp(key, "ota_port") == 0) config_store_set_ota_port(atoi(val));
    else if (strcmp(key, "cfg_status") == 0) config_store_set_status(strtoul(val, NULL, 0));
    else if (strncmp(key, "dd_", 3) == 0 && strlen(key) >= 6) {
        int idx = key[3] - '0';
        if (idx >= 0 && idx < 8) {
        int16_t v_on, v_off;
        config_store_get_duskdawn(idx, &v_on, &v_off);
        if (key[4] == '_') {
            if (key[5] == 'o' && key[6] == 'n')       v_on = atoi(val);
            else if (key[5] == 'o' && key[6] == 'f')  v_off = atoi(val);
        }
        config_store_set_duskdawn(idx, v_on, v_off);
    }

    } else {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "unknown key"); return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t testmail_handler(httpd_req_t *req)
{
    char srv[64], snd[64], pwd[64], rcpt[64];
    int port = 465;
    config_store_get_all_smtp(srv, sizeof(srv), &port, snd, sizeof(snd), pwd, sizeof(pwd), rcpt, sizeof(rcpt));

    smtp_cfg_t cfg = {
        .server    = srv,
        .port      = port,
        .user      = snd,
        .pass      = pwd,
        .recipient = rcpt,
    };

    ESP_LOGI(TAG, "Sending test mail via %s:%d from %s to %s", srv, port, snd, rcpt);
    esp_err_t err = smtp_send(&cfg, "ESP32 Test-Mail", "Hallo vom ESP32! Der E-Mail-Versand funktioniert.");

    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    if (err == ESP_OK) {
        return httpd_resp_sendstr(req, "OK: Test-Mail wurde gesendet!");
    } else {
        return httpd_resp_sendstr(req, "FEHLER: Versand fehlgeschlagen (siehe serielles Log / Port 465 testen)");
    }
}

static esp_err_t ota_check_handler(httpd_req_t *req)
{
    ota_client_check_now();
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    return httpd_resp_sendstr(req, "OTA-Check auf Server gestartet (siehe serielles Log; bei neuer Version erfolgt automatischer Neustart)...");
}


