#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

unsigned long config_store_get_debug_level(void);
void config_store_set_debug_level(unsigned long level);

unsigned long config_store_get_display_timeout(void);
void config_store_set_display_timeout(unsigned long minutes);

void config_store_get_display_activity(uint8_t *start_h, uint8_t *start_m,
                                       uint8_t *end_h,   uint8_t *end_m);
void config_store_set_display_activity(uint8_t start_h, uint8_t start_m,
                                       uint8_t end_h,   uint8_t end_m);

void config_store_get_ntp_server(char *buf, size_t len);
void config_store_set_ntp_server(const char *server);

int config_store_get_ntp_timezone(void);
void config_store_set_ntp_timezone(int tz);

long config_store_get_temperature_offset(void);
void config_store_set_temperature_offset(long offset);

long config_store_get_humidity_offset(void);
void config_store_set_humidity_offset(long offset);

void config_store_get_deployment(char *loc, size_t len);
void config_store_set_deployment(const char *loc);

double config_store_get_latitude(void);
void config_store_set_latitude(double lat);

double config_store_get_longitude(void);
void config_store_set_longitude(double lon);

unsigned long config_store_get_status(void);
void config_store_set_status(unsigned long bits);

void config_store_get_smtp_server(char *buf, size_t len);
void config_store_set_smtp_server(const char *srv);
int config_store_get_smtp_port(void);
void config_store_set_smtp_port(int port);
void config_store_get_smtp_sender(char *buf, size_t len);
void config_store_set_smtp_sender(const char *s);
void config_store_get_smtp_password(char *buf, size_t len);
void config_store_set_smtp_password(const char *s);
void config_store_get_smtp_recipient(char *buf, size_t len);
void config_store_set_smtp_recipient(const char *s);
void config_store_get_all_smtp(char *server, size_t slen, int *port,
                                char *sender, size_t dlen,
                                char *pass, size_t plen,
                                char *recipient, size_t rlen);
void config_store_set_all_smtp(const char *server, int port,
                                const char *sender, const char *pass,
                                const char *recipient);

void config_store_get_ota_server(char *buf, size_t len);
void config_store_set_ota_server(const char *s);
int config_store_get_ota_port(void);
void config_store_set_ota_port(int port);
void config_store_get_all_ota(char *server, size_t slen, int *port);
void config_store_set_all_ota(const char *server, int port);

void config_store_get_ntp_all(char *server, size_t slen, int *tz);
void config_store_set_ntp_all(const char *server, int tz);

typedef struct {
    int16_t offset_min_on;
    int16_t offset_min_off;
} DuskDawnPair;

void config_store_get_duskdawn(uint8_t idx, int16_t *off_on, int16_t *off_off);
void config_store_set_duskdawn(uint8_t idx, int16_t off_on, int16_t off_off);
void config_store_get_all_duskdawn(DuskDawnPair *pairs, uint8_t count);
void config_store_set_all_duskdawn(const DuskDawnPair *pairs, uint8_t count);

#ifdef __cplusplus
}
#endif