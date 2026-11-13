idf_component_register(
    SRCS "app_main.c" "wifi.c" "ntp.c" "relay.c" "display.c" "dht22.c" "httpd.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_rom app_update esp_http_server esp_wifi esp_event esp_netif nvs_flash esp-idf-lib__dht espressif__esp_lcd_ili9341
    PRIV_REQUIRES DS18B20 wifi_ap dns_server
)

# Subcomponents
add_subdirectory DS18B20
add_subdirectory wifi_ap
add_subdirectory dns_server
