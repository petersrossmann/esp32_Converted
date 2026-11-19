/*
 * smtp_client.c — minimal SMTP client for ESP-IDF.
 *
 * Sends email via STARTTLS or plain SMTP using ESP-TLS.
 * License: Beerware (same as the original ESP8266 email.c).
 */
#include "smtp_client.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

static const char *TAG = "smtp";

#define BUF_SZ 512

static char *b64_encode(const char *in, size_t len)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = ((len + 2) / 3) * 4 + 1;
    char *out = calloc(1, out_len);
    if (!out) return NULL;
    size_t i, j = 0;
    for (i = 0; i < len; i += 3) {
        int a = in[i];
        int b = i + 1 < len ? in[i + 1] : 0;
        int c = i + 2 < len ? in[i + 2] : 0;
        out[j++] = b64[(a >> 2) & 0x3f];
        out[j++] = b64[((a << 4) | (b >> 4)) & 0x3f];
        out[j++] = i + 1 < len ? b64[((b << 2) | (c >> 6)) & 0x3f] : '=';
        out[j++] = i + 2 < len ? b64[c & 0x3f] : '=';
    }
    out[j] = 0;
    return out;
}

static int recv_smtp_reply(esp_tls_t *tls, char *buf, size_t sz)
{
    int code = 0;
    while (1) {
        size_t i = 0;
        while (i < sz - 1) {
            int r = esp_tls_conn_read(tls, buf + i, 1);
            if (r <= 0) {
                buf[i] = '\0';
                return code;
            }
            if (buf[i] == '\n') break;
            i++;
        }
        buf[i] = '\0';
        /* Trailing \r entfernen für sauberes Log */
        if (i > 0 && buf[i - 1] == '\r') buf[i - 1] = '\0';
        ESP_LOGI(TAG, "SMTP <- %s", buf);

        if (i >= 3) {
            code = atoi(buf);
            /* Bei SMTP endet eine mehrzeilige Antwort erst, wenn nach dem 3-stelligen Code ein Leerzeichen folgt (nicht '-') */
            if (buf[3] == ' ' || buf[3] == '\0') {
                break;
            }
        } else {
            break;
        }
    }
    return code;
}

static int send_cmd(esp_tls_t *tls, const char *fmt, ...)
{
    char buf[BUF_SZ];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    esp_tls_conn_write(tls, buf, n);
    return recv_smtp_reply(tls, buf, sizeof(buf));
}


esp_err_t smtp_send(const smtp_cfg_t *cfg, const char *subject, const char *body)
{
    
    if (!cfg || cfg->port <= 0 || !cfg->server || cfg->server[0] == '\0') {
        ESP_LOGW(TAG, "SMTP disabled or invalid config (port=%d)", cfg ? cfg->port : -1);
        return ESP_ERR_INVALID_ARG;
    }

    esp_tls_cfg_t tls_cfg = {
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_tls_t *tls = esp_tls_init();
    if (!tls) {
        ESP_LOGE(TAG, "esp_tls_init failed");
        return ESP_ERR_NO_MEM;
    }

    int ret = esp_tls_conn_new_sync(cfg->server, strlen(cfg->server),
                                     cfg->port, &tls_cfg, tls);
    if (ret != 1) {
        ESP_LOGE(TAG, "TLS connect failed to %s:%d (err %d)", cfg->server, cfg->port, ret);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    char buf[BUF_SZ];
    recv_smtp_reply(tls, buf, sizeof(buf)); /* 220 Greeting abholen */

    send_cmd(tls, "EHLO %s\r\n", cfg->server);
    send_cmd(tls, "AUTH LOGIN\r\n");

    char *b64_user = b64_encode(cfg->user, strlen(cfg->user));
    char *b64_pass = b64_encode(cfg->pass, strlen(cfg->pass));
    if (!b64_user || !b64_pass) {
        free(b64_user); free(b64_pass);
        esp_tls_conn_destroy(tls);
        return ESP_ERR_NO_MEM;
    }

    send_cmd(tls, "%s\r\n", b64_user);
    int auth_code = send_cmd(tls, "%s\r\n", b64_pass);
    free(b64_user); free(b64_pass);

    if (auth_code != 235) {
        ESP_LOGE(TAG, "SMTP Auth failed (code %d)", auth_code);
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    if (send_cmd(tls, "MAIL FROM:<%s>\r\n", cfg->user) != 250 ||
        send_cmd(tls, "RCPT TO:<%s>\r\n", cfg->recipient) != 250 ||
        send_cmd(tls, "DATA\r\n") != 354) {
        ESP_LOGE(TAG, "SMTP envelope rejected");
        esp_tls_conn_destroy(tls);
        return ESP_FAIL;
    }

    /* Mail-Header + Body + Punkt senden und 250 Queued abwarten! */
    int data_code = send_cmd(tls,
        "From: %s\r\nTo: %s\r\nSubject: %s\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\n%s\r\n.\r\n",
        cfg->user, cfg->recipient, subject, body);

    send_cmd(tls, "QUIT\r\n");
    esp_tls_conn_destroy(tls);

    if (data_code != 250) {
        ESP_LOGE(TAG, "SMTP DATA rejected (code %d)", data_code);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "email queued successfully for %s", cfg->recipient);
    return ESP_OK;

}