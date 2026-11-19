#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *server;     /* SMTP server hostname */
    int         port;       /* port (465 for TLS, 587 for STARTTLS) */
    const char *user;       /* sender email / username */
    const char *pass;       /* password */
    const char *recipient;  /* recipient email */
} smtp_cfg_t;

esp_err_t smtp_send(const smtp_cfg_t *cfg, const char *subject, const char *body);

#ifdef __cplusplus
}
#endif