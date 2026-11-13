#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NTP_SERVER "pool.ntp.org"

esp_err_t ntp_init(void);
void ntp_start(void);
void ntp_update_time(void);
bool ntp_is_synced(void);
const char *ntp_get_time_str(void);
void ntp_get_actual_time(int *hour, int *minute, int *wday);

#ifdef __cplusplus
}
#endif