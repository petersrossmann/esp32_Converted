/*
 * tpl_handler.c — embedded flash template handler with %KEY% substitution
 */
#include "tpl_handler.h"
#include "config_store.h"
#include "display.h"
#include "ntp.h"
#include "relay.h"
#include "esp_ota_ops.h"
#include "wifi.h"
#include "dht22.h"
#include "bme280.h"
#include "config.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define SZ_TMP 16
#define SZ_HEX 3
#define SZ_BUF 64
#define SZ_VAL 16
#define SZ_KEY 32
#define SZ_KY  16

extern const char config_tpl_start[]       asm("_binary_config_tpl_start");
extern const char timeschedule_tpl_start[] asm("_binary_timeschedule_tpl_start");
extern const char duskdawn_tpl_start[]     asm("_binary_duskdawn_tpl_start");
extern const char display_tpl_start[]      asm("_binary_display_tpl_start");
extern const char debug_tpl_start[]        asm("_binary_debug_tpl_start");

/* Day names in display order (Mon-Sun) and their bit positions in status (Sun=bit0..Sat=bit6) */
static const char *s_days[] = {"mon","tue","wed","thu","fri","sat","sun"};
static const int s_daybits[] = {1,2,3,4,5,6,0};

static void u32_to_str(unsigned long val, char *buf)
{
    char tmp[SZ_TMP];
    int i = 0;
    do {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    } while (val > 0);
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

static void i32_to_str(long val, char *buf)
{
    if (val < 0) {
        *buf++ = '-';
        u32_to_str((unsigned long)(-val), buf);
    } else {
        u32_to_str((unsigned long)val, buf);
    }
}

static void url_decode(char *str)
{
    char *src = str, *dst = str;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)*(src + 1)) && isxdigit((unsigned char)*(src + 2))) {
            char hex[SZ_HEX];
            *hex = *(src + 1);
            *(hex + 1) = *(src + 2);
            *(hex + 2) = '\0';
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

static const char *tpl_key(const char *key, float temp, float hum)
{
    static char buf[SZ_BUF];
    *buf = 0;

    if (strcmp(key, "deploy") == 0) config_store_get_deployment(buf, sizeof(buf));
    else if (strcmp(key, "lat") == 0) snprintf(buf, sizeof(buf), "%g", config_store_get_latitude());
    else if (strcmp(key, "lon") == 0) snprintf(buf, sizeof(buf), "%g", config_store_get_longitude());
    else if (strcmp(key, "DebugLevel") == 0) u32_to_str(config_store_get_debug_level(), buf);
    else if (strcmp(key, "TemperatureOffset") == 0) i32_to_str(config_store_get_temperature_offset(), buf);
    else if (strcmp(key, "HumidityOffset") == 0) i32_to_str(config_store_get_humidity_offset(), buf);
    else if (strcmp(key, "DisplayTimeout") == 0) u32_to_str(config_store_get_display_timeout(), buf);
    else if (strcmp(key, "NtpServer") == 0) config_store_get_ntp_server(buf, sizeof(buf));
    else if (strcmp(key, "Timezone") == 0) i32_to_str(config_store_get_ntp_timezone(), buf);
    else if (strcmp(key, "SmtpServer") == 0) config_store_get_smtp_server(buf, sizeof(buf));
    else if (strcmp(key, "SmtpPort") == 0) i32_to_str(config_store_get_smtp_port(), buf);
    else if (strcmp(key, "SmtpSender") == 0) config_store_get_smtp_sender(buf, sizeof(buf));
    else if (strcmp(key, "SmtpPassword") == 0) config_store_get_smtp_password(buf, sizeof(buf));
    else if (strcmp(key, "SmtpRecipient") == 0) config_store_get_smtp_recipient(buf, sizeof(buf));
    else if (strcmp(key, "OtaServer") == 0) config_store_get_ota_server(buf, sizeof(buf));
    else if (strcmp(key, "OtaPort") == 0) i32_to_str(config_store_get_ota_port(), buf);
    else if (strcmp(key, "DspAct0") == 0) { uint8_t a,b,c,d; config_store_get_display_activity(&a,&b,&c,&d); u32_to_str(a, buf); }
    else if (strcmp(key, "DspAct1") == 0) { uint8_t a,b,c,d; config_store_get_display_activity(&a,&b,&c,&d); u32_to_str(b, buf); }
    else if (strcmp(key, "DspAct2") == 0) { uint8_t a,b,c,d; config_store_get_display_activity(&a,&b,&c,&d); u32_to_str(c, buf); }
    else if (strcmp(key, "DspAct3") == 0) { uint8_t a,b,c,d; config_store_get_display_activity(&a,&b,&c,&d); u32_to_str(d, buf); }
    else if (strcmp(key, "temp") == 0) snprintf(buf, sizeof(buf), "%.1f", temp);
    else if (strcmp(key, "hum") == 0) snprintf(buf, sizeof(buf), "%.1f", hum);
    else if (strcmp(key, "ver") == 0) return esp_app_get_description()->version;
    else if (strcmp(key, "relay") == 0) return relay_get() ? "ON" : "OFF";
    else if (strcmp(key, "time") == 0) return ntp_get_time_str();
    else if (strcmp(key, "DST") == 0) return (config_store_get_status() & 2) ? " checked" : "";
    else if (strcmp(key, "heap") == 0) {
        static char hb[SZ_TMP];
        u32_to_str((unsigned long)esp_get_free_heap_size(), hb);
        return hb;
    }
    else return "";
    return buf;
}
static char *fill_template(const char *html, int *out_len, float temp, float hum)
{
    if (!html) return NULL;
    size_t sz = strlen(html);

    int out_cap = (int)sz * 2 + 1024;
    char *out = malloc(out_cap);
    if (!out) return NULL;

    static char key[SZ_BUF];
    int oi = 0;
    for (int i = 0; html[i]; ) {
        if (html[i] == '%') {
            int j = i + 1;
            while (html[j] && (isalnum((unsigned char)html[j]) || html[j] == '_')) j++;
            int klen = j - i - 1;
            if (html[j] == '%' && klen > 0 && klen < (int)sizeof(key)) {
                memcpy(key, html + i + 1, klen);
                key[klen] = 0;
                const char *v = tpl_key(key, temp, hum);
                size_t vlen = strlen(v);
                if (oi + vlen < (size_t)(out_cap - 1)) {
                    memcpy(out + oi, v, vlen);
                    oi += vlen;
                }
                i = j + 1;
                continue;
            }
        }
        if (oi < out_cap - 1) out[oi++] = html[i];
        i++;
    }
    out[oi] = 0;
    *out_len = oi;
    return out;
}

static int serve_template(httpd_req_t *req, const char *tpl_start)
{
    float temp = 0, hum = 0;
    dht_read(&temp, &hum);
    int len;
    char *out = fill_template(tpl_start, &len, temp, hum);
    if (!out) { httpd_resp_send_err(req, 500, "tpl error"); return ESP_FAIL; }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, out, len);
    free(out);
    return ESP_OK;
}

static esp_err_t redirect_to(httpd_req_t *req, const char *location)
{
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_set_type(req, HTTPD_TYPE_TEXT);
    return httpd_resp_sendstr(req, "Redirecting...");
}

/* config CGI */
esp_err_t tpl_config_cgi(httpd_req_t *req)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen > 1024) { httpd_resp_send_err(req, 400, "bad"); return ESP_FAIL; }
    char *qry = malloc(qlen + 1);
    if (!qry) { httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }
    httpd_req_get_url_query_str(req, qry, qlen + 1);

    char *v = malloc(128);
    if (!v) { free(qry); httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }

    if (httpd_query_key_value(qry, "Depl1", v, 128) == ESP_OK) { url_decode(v); config_store_set_deployment(v); }
    if (httpd_query_key_value(qry, "Lat1", v, 128) == ESP_OK) config_store_set_latitude(atof(v));
    if (httpd_query_key_value(qry, "Lon1", v, 128) == ESP_OK) config_store_set_longitude(atof(v));
    if (httpd_query_key_value(qry, "NtpServer", v, 128) == ESP_OK) { url_decode(v); config_store_set_ntp_server(v); }
    if (httpd_query_key_value(qry, "Timezone", v, 128) == ESP_OK) config_store_set_ntp_timezone(atoi(v));
    if (httpd_query_key_value(qry, "DebugLevel", v, 128) == ESP_OK) config_store_set_debug_level(atol(v));
    if (httpd_query_key_value(qry, "TemperatureOffset", v, 128) == ESP_OK) config_store_set_temperature_offset(atol(v));
    if (httpd_query_key_value(qry, "HumidityOffset", v, 128) == ESP_OK) config_store_set_humidity_offset(atol(v));
    if (httpd_query_key_value(qry, "DisplayTimeout", v, 128) == ESP_OK) config_store_set_display_timeout(atol(v));
    if (httpd_query_key_value(qry, "serv", v, 128) == ESP_OK) { url_decode(v); config_store_set_smtp_server(v); }
    if (httpd_query_key_value(qry, "port", v, 128) == ESP_OK) config_store_set_smtp_port(atoi(v));
    if (httpd_query_key_value(qry, "sender", v, 128) == ESP_OK) { url_decode(v); config_store_set_smtp_sender(v); }
    if (httpd_query_key_value(qry, "passwd", v, 128) == ESP_OK) { url_decode(v); config_store_set_smtp_password(v); }
    if (httpd_query_key_value(qry, "recipi", v, 128) == ESP_OK) { url_decode(v); config_store_set_smtp_recipient(v); }
    if (httpd_query_key_value(qry, "OTA_serv", v, 128) == ESP_OK) { url_decode(v); config_store_set_ota_server(v); }
    if (httpd_query_key_value(qry, "OTA_port", v, 128) == ESP_OK) config_store_set_ota_port(atoi(v));

    unsigned long st = config_store_get_status();
    if (httpd_query_key_value(qry, "DST", v, 128) == ESP_OK) st |= 2; else st &= ~2UL;
    bool do_reset = (httpd_query_key_value(qry, "InstantReset", v, 128) == ESP_OK);
    config_store_set_status(st);

    uint8_t d0 = 8, d1 = 0, d2 = 22, d3 = 0;
    if (httpd_query_key_value(qry, "DspAct0", v, 128) == ESP_OK) d0 = atoi(v);
    if (httpd_query_key_value(qry, "DspAct1", v, 128) == ESP_OK) d1 = atoi(v);
    if (httpd_query_key_value(qry, "DspAct2", v, 128) == ESP_OK) d2 = atoi(v);
    if (httpd_query_key_value(qry, "DspAct3", v, 128) == ESP_OK) d3 = atoi(v);
    config_store_set_display_activity(d0, d1, d2, d3);

    free(v);
    free(qry);

    redirect_to(req, "/config.tpl");

    if (do_reset) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
    return ESP_OK;
}

/* Template GET handlers */
esp_err_t tpl_config_get(httpd_req_t *req)  { return serve_template(req, config_tpl_start); }
esp_err_t tpl_display_get(httpd_req_t *req) { return serve_template(req, display_tpl_start); }
esp_err_t tpl_debug_get(httpd_req_t *req)   { return serve_template(req, debug_tpl_start); }

/* GET /timeschedule.tpl — build 8 scheduler rows with checkboxes */
esp_err_t tpl_timesched_get(httpd_req_t *req) {
    const char *html = timeschedule_tpl_start;
    size_t sz = strlen(html);

    TimeListElt *tl = ntp_get_timelist();
    const int ROWS_SZ = 8 * 800;
    char *rows = malloc(ROWS_SZ);
    if (!rows) { httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }
    int ri = 0;

    for (int i = 0; i < 8; i++) {
        if (ri > ROWS_SZ - 800) break;
        uint16_t st = tl[i].status;
        ri += snprintf(rows + ri, ROWS_SZ - ri,
            "<tr><td>%d</td>"
            "<td><input type=\"checkbox\" name=\"s%d_act\" value=\"1\"%s></td>",
            i, i, (st & 0x8000) ? " checked" : "");
        for (int d = 0; d < 7; d++) {
            ri += snprintf(rows + ri, ROWS_SZ - ri,
                "<td><input type=\"checkbox\" name=\"s%d_%s\" value=\"1\"%s></td>",
                i, s_days[d], (st & (1 << s_daybits[d])) ? " checked" : "");
        }
        uint8_t h_on  = (tl[i].hour_on   > 23) ? 0 : tl[i].hour_on;
        uint8_t m_on  = (tl[i].minute_on > 59) ? 0 : tl[i].minute_on;
        uint8_t h_off = (tl[i].hour_off  > 23) ? 0 : tl[i].hour_off;
        uint8_t m_off = (tl[i].minute_off > 59) ? 0 : tl[i].minute_off;
        uint16_t run = (tl[i].runtime == 255) ? 0 : tl[i].runtime;

        ri += snprintf(rows + ri, ROWS_SZ - ri,
            "<td><input name=\"s%dh_on\" value=\"%02d\" size=\"2\"></td>"
            "<td><input name=\"s%dmin_on\" value=\"%02d\" size=\"2\"></td>"
            "<td><input name=\"s%dh_off\" value=\"%02d\" size=\"2\"></td>"
            "<td><input name=\"s%dmin_off\" value=\"%02d\" size=\"2\"></td>"
            "<td><input name=\"s%drun\" value=\"%d\" size=\"3\"></td></tr>\n",
            i, h_on, i, m_on,
            i, h_off, i, m_off,
            i, run);
    }

    char *out = malloc(sz + ri + 256);
    if (!out) { free(rows); httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }
    char *p = strstr(html, "%sched_rows%");
    if (p) {
        int prefix = p - html;
        memcpy(out, html, prefix);
        memcpy(out + prefix, rows, ri);
        strcpy(out + prefix + ri, p + 12);
    } else {
        strcpy(out, html);
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, out, strlen(out));
    free(rows); free(out);
    return ESP_OK;
}

/* GET /duskdawn.tpl — build 8 dusk/dawn rows with day checkboxes */
esp_err_t tpl_duskdawn_get(httpd_req_t *req) {
    const char *html = duskdawn_tpl_start;
    size_t sz = strlen(html);

    TimeListElt *tl = ntp_get_timelist();
    const int ROWS_SZ = 8 * 1024;
    char *rows = malloc(ROWS_SZ);
    if (!rows) { httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }
    int ri = 0;

    for (int i = 8; i < 16; i++) {
        if (ri > ROWS_SZ - 1024) break;
        uint16_t st = tl[i].status;
        ri += snprintf(rows + ri, ROWS_SZ - ri,
            "<tr><td>%d</td>"
            "<td><input type=\"checkbox\" name=\"dd%d_act\" value=\"1\"%s></td>"
            "<td><input type=\"checkbox\" name=\"dd%d_dawn\" value=\"1\"%s></td>"
            "<td><input type=\"checkbox\" name=\"dd%d_dusk\" value=\"1\"%s></td>",
            i - 8,
            i, (st & 0x8000) ? " checked" : "",
            i, (st & (1 << 14)) ? " checked" : "",
            i, (st & (1 << 13)) ? " checked" : "");
        for (int d = 0; d < 7; d++) {
            ri += snprintf(rows + ri, ROWS_SZ - ri,
                "<td><input type=\"checkbox\" name=\"dd%d_%s\" value=\"1\"%s></td>",
                i, s_days[d], (st & (1 << s_daybits[d])) ? " checked" : "");
        }
        int16_t on_off = 0, off_off = 0;
        config_store_get_duskdawn(i - 8, &on_off, &off_off);
        if (on_off == -1 || on_off == 255) on_off = 0;
        if (off_off == -1 || off_off == 255) off_off = 0;

        ri += snprintf(rows + ri, ROWS_SZ - ri,
            "<td><input name=\"dd%d_on\" value=\"%02d\" size=\"3\"></td>"
            "<td><input name=\"dd%d_off\" value=\"%02d\" size=\"3\"></td></tr>\n",
            i, on_off, i, off_off);
    }

    char *out = malloc(sz + ri + 256);
    if (!out) { free(rows); httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }
    char *p = strstr(html, "%sched_rows%");
    if (p) {
        int prefix = p - html;
        memcpy(out, html, prefix);
        memcpy(out + prefix, rows, ri);
        strcpy(out + prefix + ri, p + 12);
    } else {
        strcpy(out, html);
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, out, strlen(out));
    free(rows); free(out);
    return ESP_OK;
}

/* CGI handlers */
esp_err_t tpl_timesched_cgi(httpd_req_t *req) {
    size_t qlen = req->content_len;
    if (qlen == 0 || qlen > 2048) { httpd_resp_send_err(req, 400, "bad"); return ESP_FAIL; }
    char *qry = malloc(qlen + 1);
    if (!qry) { httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }
    int ret = httpd_req_recv(req, qry, qlen);
    if (ret <= 0) { free(qry); httpd_resp_send_err(req, 500, "recv"); return ESP_FAIL; }
    qry[ret] = '\0';

    TimeListElt *tl = ntp_get_timelist();
    char v[SZ_VAL];
    for (int i = 0; i < 8; i++) {
        uint16_t st = 0;
        char key[SZ_KEY];
        snprintf(key, SZ_KEY, "s%d_act", i);
        if (httpd_query_key_value(qry, key, v, SZ_VAL) == ESP_OK) st |= 0x8000;
        for (int d = 0; d < 7; d++) {
            snprintf(key, SZ_KEY, "s%d_%s", i, s_days[d]);
            if (httpd_query_key_value(qry, key, v, SZ_VAL) == ESP_OK) st |= (1 << s_daybits[d]);
        }
        tl[i].status = st;
        snprintf(key, SZ_KEY, "s%dh_on", i);
        if (httpd_query_key_value(qry, key, v, SZ_VAL) == ESP_OK) tl[i].hour_on = atoi(v);
        snprintf(key, SZ_KEY, "s%dmin_on", i);
        if (httpd_query_key_value(qry, key, v, SZ_VAL) == ESP_OK) tl[i].minute_on = atoi(v);
        snprintf(key, SZ_KEY, "s%dh_off", i);
        if (httpd_query_key_value(qry, key, v, SZ_VAL) == ESP_OK) tl[i].hour_off = atoi(v);
        snprintf(key, SZ_KEY, "s%dmin_off", i);
        if (httpd_query_key_value(qry, key, v, SZ_VAL) == ESP_OK) tl[i].minute_off = atoi(v);
        snprintf(key, SZ_KEY, "s%drun", i);
        if (httpd_query_key_value(qry, key, v, SZ_VAL) == ESP_OK) tl[i].runtime = atoi(v);
    }
    ntp_save_timelist();
    free(qry);
    return redirect_to(req, "/timeschedule.tpl");
}

esp_err_t tpl_duskdawn_cgi(httpd_req_t *req) {
    size_t qlen = req->content_len;
    if (qlen == 0 || qlen > 1024) { httpd_resp_send_err(req, 400, "bad"); return ESP_FAIL; }
    char *qry = malloc(qlen + 1);
    if (!qry) { httpd_resp_send_err(req, 500, "oom"); return ESP_FAIL; }
    int ret = httpd_req_recv(req, qry, qlen);
    if (ret <= 0) { free(qry); httpd_resp_send_err(req, 500, "recv"); return ESP_FAIL; }
    qry[ret] = '\0';

    TimeListElt *tl = ntp_get_timelist();
    char v[SZ_VAL];
    for (int i = 8; i < 16; i++) {
        uint16_t st = 0;
        char ky[SZ_KY];
        snprintf(ky, SZ_KY, "dd%d_act", i);
        if (httpd_query_key_value(qry, ky, v, SZ_VAL) == ESP_OK) st |= 0x8000;
        snprintf(ky, SZ_KY, "dd%d_dawn", i);
        if (httpd_query_key_value(qry, ky, v, SZ_VAL) == ESP_OK) st |= (1 << 14);
        snprintf(ky, SZ_KY, "dd%d_dusk", i);
        if (httpd_query_key_value(qry, ky, v, SZ_VAL) == ESP_OK) st |= (1 << 13);
        for (int d = 0; d < 7; d++) {
            snprintf(ky, SZ_KY, "dd%d_%s", i, s_days[d]);
            if (httpd_query_key_value(qry, ky, v, SZ_VAL) == ESP_OK) st |= (1 << s_daybits[d]);
        }
        tl[i].status = st;

        int16_t on = 0, off = 0;
        config_store_get_duskdawn(i - 8, &on, &off);
        snprintf(ky, SZ_KY, "dd%d_on", i);
        if (httpd_query_key_value(qry, ky, v, SZ_VAL) == ESP_OK) on = atoi(v);
        snprintf(ky, SZ_KY, "dd%d_off", i);
        if (httpd_query_key_value(qry, ky, v, SZ_VAL) == ESP_OK) off = atoi(v);
        config_store_set_duskdawn(i - 8, on, off);
    }
    ntp_save_timelist();
    free(qry);
    return redirect_to(req, "/duskdawn.tpl");
}

