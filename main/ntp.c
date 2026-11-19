/*
 * ntp.c — NTP time sync, DST, dusk/dawn calculation, and time scheduler.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "config.h"
#include "ntp.h"
#include "sunriset.h"
#ifdef RELAY_ENABLED
#include "relay.h"
#endif
#include "config_store.h"
#include "nvs.h"

static const char *TAG = "ntp";

static bool s_synced = false;
static time_t s_timestamp = 0;

/* Dusk/dawn cached values */
static uint8_t s_rise_hour = 0, s_rise_min = 0;
static uint8_t s_set_hour = 0, s_set_min = 0;
static bool s_duskdawn_valid = false;
static bool s_duskdawn_calculated = false;

/* Last known time for scheduler */
static uint8_t s_last_hour = 0xff, s_last_min = 0xff;

/*--------------------------------------------------------------------------*
 * Time table defines
 *--------------------------------------------------------------------------*/
#define BIT_VALID 0x8000
#define BIT_DAWN (1 << 14)
#define BIT_DUSK (1 << 13)
#define EMPTY_CELL 0x2603
#define CONFIG__BIT_AUTO_DST (1 << 1)

/* In-memory time table (16 entries: 8 fixed + 8 dusk/dawn-based) */
static TimeListElt s_timelist[16];
static bool s_timelist_initialized = false;

static void init_timelist(void)
{
    if (s_timelist_initialized) return;
    for (int i = 0; i < 16; i++) {
        s_timelist[i].status = 0;
        s_timelist[i].runtime = 255;
        s_timelist[i].hour_on = 255;
        s_timelist[i].minute_on = 255;
        s_timelist[i].hour_off = 255;
        s_timelist[i].minute_off = 255;
    }
    /* Load from NVS */
    nvs_handle_t h;
    if (nvs_open("sched", NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_timelist);
        nvs_get_blob(h, "tl", (uint8_t *)&s_timelist, &sz);
        nvs_close(h);
    }
    s_timelist_initialized = true;
}

void ntp_save_timelist(void)
{
    nvs_handle_t h;
    if (nvs_open("sched", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "tl", (uint8_t *)&s_timelist, sizeof(s_timelist));
        nvs_commit(h);
        nvs_close(h);
    }
}

TimeListElt *ntp_get_timelist(void)
{
    init_timelist();
    return s_timelist;
}

/*--------------------------------------------------------------------------*
 * Relay switching helper
 *--------------------------------------------------------------------------*/
static void switch_relay(int on_off)
{
#ifdef RELAY_ENABLED
    relay_set(on_off ? true : false);
    ESP_LOGI(TAG, "scheduler relay %s", on_off ? "ON" : "OFF");
#else
    ESP_LOGI(TAG, "scheduler would relay %s (disabled)", on_off ? "ON" : "OFF");
#endif
}

/*--------------------------------------------------------------------------*
 * Apply timezone & DST settings from config_store
 *--------------------------------------------------------------------------*/
void ntp_apply_timezone_config(void)
{
    long tz = config_store_get_ntp_timezone();
    bool auto_dst = (config_store_get_status() & CONFIG__BIT_AUTO_DST) != 0;

    /* Note: POSIX TZ signs are inverted (GMT+1 is written as UTC-1) */
    char tz_str[64];
    if (auto_dst) {
        snprintf(tz_str, sizeof(tz_str), "UTC%+ldDST%+ld,M3.5.0,M10.5.0/3", -tz, -(tz + 1));
    } else {
        snprintf(tz_str, sizeof(tz_str), "UTC%+ld", -tz);
    }

    setenv("TZ", tz_str, 1);
    tzset();

    /* Force dusk/dawn recalculation with new offset */
    s_duskdawn_calculated = false;
    ESP_LOGI(TAG, "Timezone configured: %s (GMT%+ld, Auto-DST: %s)",
             tz_str, tz, auto_dst ? "ON" : "OFF");
}

/*--------------------------------------------------------------------------*
 * Dusk/dawn calculation
 *--------------------------------------------------------------------------*/
static void calculate_duskdawn(struct tm *dt)
{
    if (!dt) {
        s_duskdawn_valid = false;
        return;
    }

    double rise, set;
    double lon = config_store_get_longitude();
    double lat = config_store_get_latitude();

    int rc = sun_rise_set(dt->tm_year + 1900, dt->tm_mon + 1, dt->tm_mday,
                          lon, lat, &rise, &set);
    if (rc == 0 || rc == 1) {
        long tz = config_store_get_ntp_timezone();
        if (dt->tm_isdst > 0) {
            tz++;
        }

        double r = rise + tz;
        double s = set  + tz;

        while (r < 0.0)   r += 24.0;
        while (r >= 24.0) r -= 24.0;
        while (s < 0.0)   s += 24.0;
        while (s >= 24.0) s -= 24.0;

        s_rise_hour = (int)r;
        s_rise_min  = (int)((r - s_rise_hour) * 60.0 + 0.5);
        if (s_rise_min == 60) {
            s_rise_min = 0;
            s_rise_hour = (s_rise_hour + 1) % 24;
        }

        s_set_hour  = (int)s;
        s_set_min   = (int)((s - s_set_hour) * 60.0 + 0.5);
        if (s_set_min == 60) {
            s_set_min = 0;
            s_set_hour = (s_set_hour + 1) % 24;
        }

        s_duskdawn_valid = true;
        s_duskdawn_calculated = true;
        ESP_LOGI(TAG, "DuskDawn (GMT%+ld): rise %02d:%02d, set %02d:%02d",
                 tz, s_rise_hour, s_rise_min, s_set_hour, s_set_min);
    } else {
        s_duskdawn_valid = false;
    }
}
/*--------------------------------------------------------------------------*
 * Night detection
 *--------------------------------------------------------------------------*/
static bool is_night(uint8_t hour, uint8_t minute)
{
    if (!s_duskdawn_valid || s_timestamp < 100000)
        return false;
    if (hour > s_set_hour || hour < s_rise_hour)
        return true;
    if (hour == s_set_hour && minute >= s_set_min)
        return true;
    return false;
}

/*--------------------------------------------------------------------------*
 * Time scheduler — checks time table and triggers relay actions
 *--------------------------------------------------------------------------*/
static void time_scheduler(struct tm *dt)
{
    if (!dt) return;
    init_timelist();

    uint8_t hour = dt->tm_hour;
    uint8_t minute = dt->tm_min;
    uint8_t wday = dt->tm_wday;

    if (hour == s_last_hour && minute == s_last_min)
        return;
    s_last_hour = hour;
    s_last_min  = minute;

    if ((hour == 0 && minute == 0) ||
        (hour == 2 && minute == 0) ||
        (hour == 3 && minute == 0) ||
        !s_duskdawn_calculated) {
        calculate_duskdawn(dt);
    }

    for (int k = 0; k < 16; k++) {
        uint16_t st = s_timelist[k].status;
        if (!(st & BIT_VALID))
            continue;

        if (!(st & (1 << wday)))
            continue;

        uint8_t on_h  = s_timelist[k].hour_on;
        uint8_t on_m  = s_timelist[k].minute_on;
        uint8_t off_h = s_timelist[k].hour_off;
        uint8_t off_m = s_timelist[k].minute_off;

        if (k >= 8) {
            if (!s_duskdawn_valid)
                continue;

            int16_t off_on_min = 0, off_off_min = 0;
            config_store_get_duskdawn(k - 8, &off_on_min, &off_off_min);

            int base_on  = (st & BIT_DAWN) ? (s_rise_hour * 60 + s_rise_min)
                         : (st & BIT_DUSK) ? (s_set_hour  * 60 + s_set_min) : -1;
            int base_off = (st & BIT_DUSK) ? (s_set_hour  * 60 + s_set_min)
                         : (st & BIT_DAWN) ? (s_rise_hour * 60 + s_rise_min) : -1;

            if (base_on < 0 || base_off < 0)
                continue;

            int total_on  = (base_on  + off_on_min  + 1440) % 1440;
            int total_off = (base_off + off_off_min + 1440) % 1440;

            on_h  = total_on / 60;
            on_m  = total_on % 60;
            off_h = total_off / 60;
            off_m = total_off % 60;
        } else if (s_timelist[k].runtime > 0 && s_timelist[k].runtime < 255) {
            int r = rand() % s_timelist[k].runtime;
            int on_min  = (on_h * 60 + on_m) + r;
            int off_min = (off_h * 60 + off_m) + r;
            if (off_min <= on_min)
                off_min += s_timelist[k].runtime * 2;
            on_h  = (on_min / 60) % 24;
            on_m  = on_min % 60;
            off_h = (off_min / 60) % 24;
            off_m = off_min % 60;
        }

        if (on_h == hour && on_m == minute) {
            switch_relay(1);
            break;
        }

        if (off_h == hour && off_m == minute) {
            switch_relay(0);
            break;
        }
    }
}

/*--------------------------------------------------------------------------*
 * SNTP sync notification callback & periodic task
 *--------------------------------------------------------------------------*/
static void ntp_sync_notification_cb(struct timeval *tv)
{
    s_synced = true;
    if (tv) {
        s_timestamp = tv->tv_sec;
    }
}

static void ntp_timer_task(void *pv)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(500));

        time_t now;
        time(&now);
        if (now < 100000)
            continue;

        s_timestamp = now;
        struct tm dt;
        localtime_r(&s_timestamp, &dt);
        time_scheduler(&dt);
    }
}

/*--------------------------------------------------------------------------*
 * Public API
 *--------------------------------------------------------------------------*/
esp_err_t ntp_init(void)
{
    srand(esp_random());
    ntp_apply_timezone_config();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);

    static char ntp_srv[64];
    config_store_get_ntp_server(ntp_srv, sizeof(ntp_srv));
    if (ntp_srv[0] == '\0') {
        config_store_set_ntp_server(NTP_SERVER);
        strncpy(ntp_srv, NTP_SERVER, sizeof(ntp_srv) - 1);
        ntp_srv[sizeof(ntp_srv) - 1] = '\0';
    }
    esp_sntp_setservername(0, ntp_srv);
    esp_sntp_setservername(1, "de.pool.ntp.org");
    sntp_set_time_sync_notification_cb(ntp_sync_notification_cb);

    xTaskCreate(ntp_timer_task, "ntp_timer", 4096, NULL, 3, NULL);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started with server: %s", ntp_srv);

    return ESP_OK;
}

void ntp_start(void)
{
    if (!esp_sntp_enabled()) {
        esp_sntp_init();
    }
}

bool ntp_is_synced(void)
{
    return s_synced;
}

void ntp_update_time(void)
{
    static bool logged_sync = false;
    time_t now;
    time(&now);
    if (now > 100000) {
        s_timestamp = now;
        if (s_synced && !logged_sync) {
            logged_sync = true;
            ESP_LOGI(TAG, "time synced: %s", ntp_get_time_str());
        }
    }
}

const char *ntp_get_time_str(void)
{
    static char buf[32];
    if (s_timestamp < 100000) {
        strcpy(buf, "--:--:--");
        return buf;
    }
    struct tm tm;
    localtime_r(&s_timestamp, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void ntp_get_actual_time(int *hour, int *minute, int *wday)
{
    if (s_timestamp < 100000) {
        if (hour) *hour = 0;
        if (minute) *minute = 0;
        if (wday) *wday = 0;
        return;
    }
    struct tm tm;
    localtime_r(&s_timestamp, &tm);
    if (hour) *hour = tm.tm_hour;
    if (minute) *minute = tm.tm_min;
    if (wday) *wday = tm.tm_wday;
}

bool ntp_is_night(void)
{
    if (s_timestamp < 100000)
        return false;
    struct tm tm;
    localtime_r(&s_timestamp, &tm);
    return is_night(tm.tm_hour, tm.tm_min);
}
