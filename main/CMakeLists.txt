idf_component_register(
    SRCS "app_main.c" "wifi.c" "ntp.c" "relay.c" "dht22.c" "httpd.c" "display.c" "DS18B20.c" "bme280.c" "smtp_client.c" "sunriset.c" "fonts.c" "config_store.c" "portstate.c" "ota_client.c" "tpl_handler.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "web/config.tpl"
                   "web/debug.tpl"
                   "web/display.tpl"
                   "web/duskdawn.tpl"
                   "web/index.html"
                   "web/timeschedule.tpl"
                   "web/favicon.ico"
    REQUIRES config driver esp_rom app_update esp_http_server esp_http_client esp-tls esp_wifi esp_event esp_netif nvs_flash esp_system esp-idf-lib__dht espressif__esp_lcd_ili9341
)
