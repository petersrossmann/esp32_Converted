# ESP32 Converted Project (ESP-WROOM-32)

This directory contains the ESP-IDF v5.0.9 project converted from the old ESP8266 code in `C:\OLD_ESP8366\ESP8266Project`. The conversion preserves functionality (ILI9341 display, DHT22/AM2301 sensor, DS18B20 temperature sensor, WiFi AP with captive portal, NTP sync, relay control, HTTP server, OTA updates) while adapting to the ESP-IDF stack.

## Features

- **Display**: ILI9341 SPI driver via Espressif `esp_lcd_ili9341` component.
- **Sensing**:
  - DHT22/AM2301 via `esp-idf-lib/dht` component.
  - DS18B20 via a dedicated 1-Wire driver (copied from existing ESP-IDF work).
- **WiFi**: AP mode with captive portal (same code as `esp32_il9341_dht/wifi_ap.c`).
- **NTP**: SNTP time sync (`esp32_il9341_dht/ntp.c`).
- **Relay**: GPIO2 relay control (active HIGH, LOW at boot for strapping).
- **HTTP Server**: `/`, `/relay`, `/wifi`, `/update` (OTA) handlers (`esp32_il9341_dht/httpd.c`).
- **OTA**: Firmware upload via HTTP POST `/update`.
- **Configuration**: `components/config/` holds ESP32‑IDF‑friendly `config.h` (keep #define concept).

## Build & Run (Windows)

```cmd
cmd.exe /c "call C:\\esp-idf\\esp-idf-v5.0.9\\esp-idf-v5.0.9\\export.bat && python %IDF_PATH%\\tools\\idf.py build"
```

After a successful build, `idf.py flash` (or JTAG) will write the binary to the ESP32.

## Directory Layout

```
esp32_Converted/
├── CMakeLists.txt                  # top-level
├── main/                           # application component
│   ├── app_main.c                  # entry point, init + tasks
│   ├── relay.c/.h                  # GPIO2 relay driver
│   ├── wifi.c/.h                   # WiFi AP + captive portal
│   ├── ntp.c/.h                    # SNTP client
│   ├── httpd.c/.h                  # HTTP server
│   ├── dht22.c/.h                  # DHT22 wrapper over esp-idf-lib/dht
│   ├── display.c/.h                # ILI9341 display driver (stub, use esp_lcd_ili9341)
│   ├── config_store.c/.h           # ESP32 config persistence (NVS/partition)
│   ├── DS18B20/                     # standalone DS18B20 driver (copied)
│   ├── wifi_ap/                     # copied from esp32_il9341_dht
│   └── dns_server/                 # copied from esp32_il9341_dht
├── components/config/               # project‑level configuration (Kconfig + header)
├── partitions.csv                  # OTA partition table (copy from existing work)
├── sdkconfig                       # build configuration (generated via menuconfig)
└── README.md
```

## Notes

- This is a **skeleton conversion**. Full details (e.g., HTTP OTA streaming, display UI rendering, DS18B20 integration in `app_main.c`) require additional porting work.
- Original ESP8266 `#define`‑based config concept lives in `components/config/include/config.h`; future work may add menuconfig or NVS backing.
- Use the existing ESP-IDF v5.0.9 components (`esp_lcd_ili9341`, `esp-idf-lib/dht`, etc.) as much as possible.

## License

Converted from the original ESP8266 code (GPLv2). This version is part of the ongoing DispEsp32 ESP‑IDF port.
