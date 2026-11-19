/*
 * config_store.c — NVS-backed configuration persistence.
 *
 * Replaces the old ESP8266 "save_to_flash" pattern.
 * All settings are stored as key-value pairs in the NVS "config" namespace,
 * readable/writable at runtime and persistent across reboots.
 */
#include "config_store.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <string.h>
#include "ntp.h"

#define NS "config"
#define MAGIC_KEY "magic"
#define MAGIC_VAL 0x2024DA7A  /* unique magic to detect initialised NVS */
static bool s_initialized = false;

static nvs_handle_t s_h;

static void apply_defaults(void)
{
    uint32_t dummy;
    if (nvs_get_u32(s_h, MAGIC_KEY, &dummy) == ESP_OK)
        return; /* already initialised */

    ESP_LOGI("cfg_store", "first boot — writing defaults");

    nvs_set_u32(s_h, MAGIC_KEY, MAGIC_VAL);

    /* NTP */
    nvs_set_str(s_h, "ntp_srv", "pool.ntp.org");
    nvs_set_u32(s_h, "ntp_tz", 1);  /* CET */

    /* SMTP */
    nvs_set_str(s_h, "smtp_srv", "mail.webclient3.de");
    nvs_set_u32(s_h, "smtp_port", 587);
    nvs_set_str(s_h, "smtp_snd", "esp32@sytec-it.de");
    nvs_set_str(s_h, "smtp_pwd", "Default");
    nvs_set_str(s_h, "smtp_rcpt", "moebius@sytec-it.de");

    /* OTA */
    nvs_set_str(s_h, "ota_srv", "ota.local");
    nvs_set_u32(s_h, "ota_port", 80);

    /* Display */
    nvs_set_u32(s_h, "disp_to", 2);
    nvs_set_u32(s_h, "disp_act", (8 << 24) | (0 << 16) | (22 << 8) | 0);

    /* Deployment */
    nvs_set_str(s_h, "deploy", "home");
    { double lat = 49.3; uint64_t l; memcpy(&l, &lat, 8); nvs_set_u64(s_h, "latitude", l); }
    { double lon = 8.7; uint64_t l; memcpy(&l, &lon, 8); nvs_set_u64(s_h, "longitude", l); }

    /* Debug */
    nvs_set_u32(s_h, "debug_level", 0);
    nvs_set_u32(s_h, "cfg_status", 0);

    /* Temperature / humidity offsets */
    nvs_set_u32(s_h, "temp_off", 0);
    nvs_set_u32(s_h, "hum_off", 0);

    nvs_commit(s_h);
}

static esp_err_t open_nvs(void)
{
    if (!s_initialized) {
        s_initialized = true;
        esp_err_t o = nvs_open(NS, NVS_READWRITE, &s_h);
        if (o == ESP_OK) {
            apply_defaults();
            nvs_close(s_h);
        }
        return o;
    }
    return nvs_open(NS, NVS_READWRITE, &s_h);
}

/*--------------------------------------------------------------------------*
 * Debug level
 *--------------------------------------------------------------------------*/
unsigned long config_store_get_debug_level(void)
{
    unsigned long val = 0;
    if (open_nvs() != ESP_OK) return val;
    nvs_get_u32(s_h, "debug_level", &val);
    nvs_close(s_h);
    return val;
}

void config_store_set_debug_level(unsigned long level)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "debug_level", level);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * Display timeout (minutes)
 *--------------------------------------------------------------------------*/
unsigned long config_store_get_display_timeout(void)
{
    unsigned long val = DISPLAY_TIMEOUT_MIN;
    if (open_nvs() != ESP_OK) return val;
    nvs_get_u32(s_h, "disp_to", &val);
    nvs_close(s_h);
    return val;
}

void config_store_set_display_timeout(unsigned long minutes)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "disp_to", minutes);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * Display activity window [start_hour, start_min, end_hour, end_min]
 *--------------------------------------------------------------------------*/
void config_store_get_display_activity(uint8_t *start_h, uint8_t *start_m,
                                       uint8_t *end_h,   uint8_t *end_m)
{
    uint32_t v = (DISPLAY_ACTIVITY_START_HOUR << 24) |
                 (DISPLAY_ACTIVITY_START_MIN  << 16) |
                 (DISPLAY_ACTIVITY_END_HOUR   << 8)  |
                 (DISPLAY_ACTIVITY_END_MIN);
    if (open_nvs() != ESP_OK) goto def;
    nvs_get_u32(s_h, "disp_act", &v);
    nvs_close(s_h);
def:
    if (start_h) *start_h = (v >> 24) & 0xff;
    if (start_m) *start_m = (v >> 16) & 0xff;
    if (end_h)   *end_h   = (v >> 8)  & 0xff;
    if (end_m)   *end_m   = v & 0xff;
}

void config_store_set_display_activity(uint8_t start_h, uint8_t start_m,
                                       uint8_t end_h,   uint8_t end_m)
{
    uint32_t v = (start_h << 24) | (start_m << 16) | (end_h << 8) | end_m;
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "disp_act", v);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * NTP server
 *--------------------------------------------------------------------------*/
void config_store_get_ntp_server(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    strncpy(buf, NTP_SERVER, len);
    if (open_nvs() != ESP_OK) return;
    nvs_get_str(s_h, "ntp_srv", buf, &len);
    nvs_close(s_h);
}

void config_store_set_ntp_server(const char *server)
{
    if (!server || open_nvs() != ESP_OK) return;
    nvs_set_str(s_h, "ntp_srv", server);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * NTP timezone offset (hours from UTC)
 *--------------------------------------------------------------------------*/
int config_store_get_ntp_timezone(void)
{
    int val = 1; /* CET default */
    if (open_nvs() != ESP_OK) return val;
    nvs_get_u32(s_h, "ntp_tz", (uint32_t *)&val);
    nvs_close(s_h);
    return val;
}

void config_store_set_ntp_timezone(int tz)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "ntp_tz", (uint32_t)tz);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * Temperature offset (tenths of a degree)
 *--------------------------------------------------------------------------*/
long config_store_get_temperature_offset(void)
{
    long val = 0;
    if (open_nvs() != ESP_OK) return val;
    nvs_get_u32(s_h, "temp_off", (uint32_t *)&val);
    nvs_close(s_h);
    return val;
}

void config_store_set_temperature_offset(long offset)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "temp_off", (uint32_t)offset);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * Humidity offset (tenths of a percent)
 *--------------------------------------------------------------------------*/
long config_store_get_humidity_offset(void)
{
    long val = 0;
    if (open_nvs() != ESP_OK) return val;
    nvs_get_u32(s_h, "hum_off", (uint32_t *)&val);
    nvs_close(s_h);
    return val;
}

void config_store_set_humidity_offset(long offset)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "hum_off", (uint32_t)offset);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * Deployment location
 *--------------------------------------------------------------------------*/
void config_store_get_deployment(char *loc, size_t len)
{
    if (!loc || len == 0) return;
    strncpy(loc, "", len);
    if (open_nvs() != ESP_OK) return;
    nvs_get_str(s_h, "deploy", loc, &len);
    nvs_close(s_h);
}

void config_store_set_deployment(const char *loc)
{
    if (!loc || open_nvs() != ESP_OK) return;
    nvs_set_str(s_h, "deploy", loc);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * Latitude / Longitude (for dusk/dawn)
 *--------------------------------------------------------------------------*/
double config_store_get_latitude(void)
{
    uint64_t val = 0;
    if (open_nvs() != ESP_OK) return 48.0;
    nvs_get_u64(s_h, "latitude", &val);
    nvs_close(s_h);
    if (val == 0) return 48.0;
    return *(double *)&val;
}

void config_store_set_latitude(double lat)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u64(s_h, "latitude", *(uint64_t *)&lat);
    nvs_commit(s_h);
    nvs_close(s_h);
}

double config_store_get_longitude(void)
{
    uint64_t val = 0;
    if (open_nvs() != ESP_OK) return 13.0;
    nvs_get_u64(s_h, "longitude", &val);
    nvs_close(s_h);
    if (val == 0) return 13.0;
    return *(double *)&val;
}

void config_store_set_longitude(double lon)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u64(s_h, "longitude", *(uint64_t *)&lon);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * Config status bits (CONFIG__BIT_*)
 *--------------------------------------------------------------------------*/
unsigned long config_store_get_status(void)
{
    unsigned long val = 0;
    if (open_nvs() != ESP_OK) return val;
    nvs_get_u32(s_h, "cfg_status", &val);
    nvs_close(s_h);
    return val;
}

void config_store_set_status(unsigned long bits)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "cfg_status", bits);
    nvs_commit(s_h);
    nvs_close(s_h);
}

/*--------------------------------------------------------------------------*
 * SMTP settings
 *--------------------------------------------------------------------------*/
void config_store_get_smtp_server(char *buf, size_t len)
{
    if (!buf || len == 0) return;
    strncpy(buf, "", len);
    if (open_nvs() != ESP_OK) return;
    nvs_get_str(s_h, "smtp_srv", buf, &len);
    nvs_close(s_h);
}

void config_store_set_smtp_server(const char *srv)
{
    if (!srv || open_nvs() != ESP_OK) return;
    nvs_set_str(s_h, "smtp_srv", srv);
    nvs_commit(s_h);
    nvs_close(s_h);
}

int config_store_get_smtp_port(void)
{
    unsigned long val = 0;
    if (open_nvs() != ESP_OK) return 0;
    nvs_get_u32(s_h, "smtp_port", &val);
    nvs_close(s_h);
    return (int)val;
}

void config_store_set_smtp_port(int port)
{
    if (open_nvs() != ESP_OK) return;
    nvs_set_u32(s_h, "smtp_port", (uint32_t)port);
    nvs_commit(s_h);
    nvs_close(s_h);
}

void config_store_get_smtp_sender(char *buf, size_t len)
{ if (!buf || len == 0) return; buf[0] = 0; if (open_nvs() != ESP_OK) return; nvs_get_str(s_h, "smtp_snd", buf, &len); nvs_close(s_h); }
void config_store_set_smtp_sender(const char *s)
{ if (!s || open_nvs() != ESP_OK) return; nvs_set_str(s_h, "smtp_snd", s); nvs_commit(s_h); nvs_close(s_h); }
void config_store_get_smtp_password(char *buf, size_t len)
{ if (!buf || len == 0) return; buf[0] = 0; if (open_nvs() != ESP_OK) return; nvs_get_str(s_h, "smtp_pwd", buf, &len); nvs_close(s_h); }
void config_store_set_smtp_password(const char *s)
{ if (!s || open_nvs() != ESP_OK) return; nvs_set_str(s_h, "smtp_pwd", s); nvs_commit(s_h); nvs_close(s_h); }
void config_store_get_smtp_recipient(char *buf, size_t len)
{ if (!buf || len == 0) return; buf[0] = 0; if (open_nvs() != ESP_OK) return; nvs_get_str(s_h, "smtp_rcpt", buf, &len); nvs_close(s_h); }
void config_store_set_smtp_recipient(const char *s)
{ if (!s || open_nvs() != ESP_OK) return; nvs_set_str(s_h, "smtp_rcpt", s); nvs_commit(s_h); nvs_close(s_h); }
void config_store_get_all_smtp(char *server, size_t slen, int *port,
                                char *sender, size_t dlen,
                                char *pass, size_t plen,
                                char *recipient, size_t rlen)
{
    config_store_get_smtp_server(server, slen);
    if (port) *port = config_store_get_smtp_port();
    config_store_get_smtp_sender(sender, dlen);
    config_store_get_smtp_password(pass, plen);
    config_store_get_smtp_recipient(recipient, rlen);
}
void config_store_set_all_smtp(const char *server, int port,
                                const char *sender, const char *pass,
                                const char *recipient)
{
    config_store_set_smtp_server(server);
    config_store_set_smtp_port(port);
    config_store_set_smtp_sender(sender);
    config_store_set_smtp_password(pass);
    config_store_set_smtp_recipient(recipient);
}

/*--------------------------------------------------------------------------*
 * OTA server
 *--------------------------------------------------------------------------*/
void config_store_get_ota_server(char *buf, size_t len)
{ if (!buf || len == 0) return; buf[0] = 0; if (open_nvs() != ESP_OK) return; nvs_get_str(s_h, "ota_srv", buf, &len); nvs_close(s_h); }
void config_store_set_ota_server(const char *s)
{ if (!s || open_nvs() != ESP_OK) return; nvs_set_str(s_h, "ota_srv", s); nvs_commit(s_h); nvs_close(s_h); }
int config_store_get_ota_port(void)
{ int val = 0; if (open_nvs() != ESP_OK) return val; nvs_get_u32(s_h, "ota_port", (uint32_t*)&val); nvs_close(s_h); return val; }
void config_store_set_ota_port(int port)
{ if (open_nvs() != ESP_OK) return; nvs_set_u32(s_h, "ota_port", (uint32_t)port); nvs_commit(s_h); nvs_close(s_h); }
void config_store_get_all_ota(char *server, size_t slen, int *port)
{ config_store_get_ota_server(server, slen); if (port) *port = config_store_get_ota_port(); }
void config_store_set_all_ota(const char *server, int port)
{ config_store_set_ota_server(server); config_store_set_ota_port(port); }

/*--------------------------------------------------------------------------*
 * NTP (whole struct)
 *--------------------------------------------------------------------------*/
void config_store_get_ntp_all(char *server, size_t slen, int *tz)
{ config_store_get_ntp_server(server, slen); if (tz) *tz = config_store_get_ntp_timezone(); }
void config_store_set_ntp_all(const char *server, int tz)
{ config_store_set_ntp_server(server); config_store_set_ntp_timezone(tz); }

/*--------------------------------------------------------------------------*
 * Dusk/Dawn offsets (8 entries)
 *--------------------------------------------------------------------------*/
void config_store_get_duskdawn(uint8_t idx, int16_t *off_on, int16_t *off_off)
{
    if (idx >= 8) return;
    uint32_t val = 0;
    if (open_nvs() != ESP_OK) return;
    char key[16]; snprintf(key, sizeof(key), "dd_%u", idx);
    nvs_get_u32(s_h, key, &val);
    nvs_close(s_h);
    if (off_on) *off_on = (int16_t)(val >> 16);
    if (off_off) *off_off = (int16_t)(val & 0xFFFF);
}
void config_store_set_duskdawn(uint8_t idx, int16_t off_on, int16_t off_off)
{
    if (idx >= 8) return;
    uint32_t val = ((uint32_t)(uint16_t)off_on << 16) | (uint16_t)off_off;
    if (open_nvs() != ESP_OK) return;
    char key[16]; snprintf(key, sizeof(key), "dd_%u", idx);
    nvs_set_u32(s_h, key, val);
    nvs_commit(s_h); nvs_close(s_h);
}
void config_store_get_all_duskdawn(DuskDawnPair *pairs, uint8_t count)
{ for (uint8_t i = 0; i < count && i < 8; i++) config_store_get_duskdawn(i, &pairs[i].offset_min_on, &pairs[i].offset_min_off); }
void config_store_set_all_duskdawn(const DuskDawnPair *pairs, uint8_t count)
{ for (uint8_t i = 0; i < count && i < 8; i++) config_store_set_duskdawn(i, pairs[i].offset_min_on, pairs[i].offset_min_off); }