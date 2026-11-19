#pragma once
#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t tpl_config_get(httpd_req_t *req);
esp_err_t tpl_config_cgi(httpd_req_t *req);
esp_err_t tpl_timesched_get(httpd_req_t *req);
esp_err_t tpl_timesched_cgi(httpd_req_t *req);
esp_err_t tpl_duskdawn_get(httpd_req_t *req);
esp_err_t tpl_duskdawn_cgi(httpd_req_t *req);
esp_err_t tpl_display_get(httpd_req_t *req);
esp_err_t tpl_debug_get(httpd_req_t *req);

#ifdef __cplusplus
}
#endif