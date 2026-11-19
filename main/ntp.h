#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NTP_SERVER "pool.ntp.org"

/* Time table entry (ported from old config.h) */
typedef struct {
    uint16_t status;    /* bit 15 active, bits 0-6: weekday, bit 13 dusk, bit 14 dawn */
    uint8_t  runtime;
    uint8_t  dummy1;
    uint8_t  hour_on;
    uint8_t  minute_on;
    uint8_t  hour_off;
    uint8_t  minute_off;
} TimeListElt;

#define BIT_VALID 0x8000

esp_err_t ntp_init(void);
void ntp_start(void);
void ntp_apply_timezone_config(void);
void ntp_update_time(void);
bool ntp_is_synced(void);
const char *ntp_get_time_str(void);
void ntp_get_actual_time(int *hour, int *minute, int *wday);
bool ntp_is_night(void);
void ntp_save_timelist(void);
TimeListElt *ntp_get_timelist(void);

#ifdef __cplusplus
}
#endif
