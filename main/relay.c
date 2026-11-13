#include "relay.h"
#include "driver/gpio.h"
#include "config.h"

static bool g_relay_state = false;

esp_err_t relay_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(RELAY_GPIO, g_relay_state ? 1 : 0);
    return ESP_OK;
}

esp_err_t relay_set(bool on)
{
    gpio_set_level(RELAY_GPIO, on ? 1 : 0);
    g_relay_state = on;
    return ESP_OK;
}

bool relay_get(void)
{
    return gpio_get_level(RELAY_GPIO) == 1;
}