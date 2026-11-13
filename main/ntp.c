#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ntp.h"

static const char *TAG = "ntp";
static bool s_synced = false;

static void ntp_sync_notification_cb(struct timeval *tv)
{
    s_synced = true;
    ESP_LOGI(TAG, "time synced");
}

esp_err_t ntp_init(void)
{
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER);
    sntp_set_time_sync_notification_cb(ntp_sync_notification_cb);
    return ESP_OK;
}

void ntp_start(void)
{
    if (!esp_sntp_enabled()) {
        esp_sntp_init();
    }
}

void ntp_update_time(void)
{
    if (esp_sntp_enabled()) return;
    ntp_start();
}

bool ntp_is_synced(void)
{
    return s_synced;
}

const char *ntp_get_time_str(void)
{
    static char buf[32];
    time_t now = time(NULL);
    if (now < 100000) {
        strcpy(buf, "--:--:--");
        return buf;
    }
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void ntp_get_actual_time(int *hour, int *minute, int *wday)
{
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (hour) *hour = tm.tm_hour;
    if (minute) *minute = tm.tm_min;
    if (wday) *wday = tm.tm_wday;
}
