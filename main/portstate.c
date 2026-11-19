#include "portstate.h"
#include "config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "relay.h"
#include "ntp.h"
#include "smtp_client.h"
#include "config_store.h"

#if defined PORTSTATE_ENABLED || defined RX_PORTSTATE_ENABLED

static const char *TAG = "portstate";
static int s_last_state = -1;   /* -1 = unknown */
static int s_cooldown = 0;
static int s_last_relay = -1;

#if defined RX_PORTSTATE_ENABLED
#define ACTIVE_PORT_GPIO RX_PORTSTATE_GPIO
#else
#define ACTIVE_PORT_GPIO PORTSTATE_GPIO
#endif

static bool check_activity(void)
{
    int hour, minute, wday;
    ntp_get_actual_time(&hour, &minute, &wday);

    uint8_t sh = DISPLAY_ACTIVITY_START_HOUR, sm = 0;
    uint8_t eh = DISPLAY_ACTIVITY_END_HOUR, em = 0;
    config_store_get_display_activity(&sh, &sm, &eh, &em);

    int now_min   = hour * 60 + minute;
    int start_min = sh * 60 + sm;
    int end_min   = eh * 60 + em;
    return (now_min >= start_min && now_min <= end_min);
}

int portstate_read(void)
{
    return gpio_get_level(ACTIVE_PORT_GPIO);
}

static void portstate_task(void *pv)
{
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* Initialzustand merken, damit beim Booten keine Fehl-Mail/Schaltung ausgelöst wird */
    s_last_state = portstate_read();
    ESP_LOGI(TAG, "Initial contact state: %s", s_last_state ? "OPEN" : "CLOSED");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s_cooldown > 0) { s_cooldown--; continue; }

        /* If relay state changed recently, skip detection for 10s */
#if defined RELAY_ENABLED
        int current_relay = relay_get() ? 1 : 0;
        if (current_relay != s_last_relay && s_last_relay >= 0) {
            s_last_relay = current_relay;
            s_cooldown = 10;
            continue;
        }
        s_last_relay = current_relay;
#endif

        int state = portstate_read();
        if (state < 0) continue;

        if (state != s_last_state) {
            s_last_state = state;

            if (state == 1) {
                /* Contact opened */
                ESP_LOGI(TAG, "Contact OPEN");

#if defined PORTSTATE_EMAIL
                {
                    char srv[64], usr[64], pwd[64], rcp[64];
                    int port = 0;
                    config_store_get_all_smtp(srv, sizeof(srv), &port, usr, sizeof(usr),
                                               pwd, sizeof(pwd), rcp, sizeof(rcp));
                    if (srv[0] != 0 && rcp[0] != 0) {
                        smtp_cfg_t sc = { .server = srv, .port = port,
                                          .user = usr, .pass = pwd, .recipient = rcp };
                        smtp_send(&sc, "Contact", "Port OPEN");
                    }
                }
#endif

#if defined SWITCH_ON_CONTACT
#if defined SWITCH_ON_CONTACT_NIGHT
                if (ntp_is_night() && check_activity()) {
                    relay_set(true);
                    s_cooldown = 100;
                }
#else
                if (check_activity()) {
                    relay_set(true);
                    s_cooldown = 20;
                }
#endif
#endif
            } else {
                /* Contact closed */
                ESP_LOGI(TAG, "Contact CLOSED");

#if defined PORTSTATE_EMAIL
                {
                    char srv[64], usr[64], pwd[64], rcp[64];
                    int port = 0;
                    config_store_get_all_smtp(srv, sizeof(srv), &port, usr, sizeof(usr),
                                               pwd, sizeof(pwd), rcp, sizeof(rcp));
                    if (srv[0] != 0 && rcp[0] != 0) {
                        smtp_cfg_t sc = { .server = srv, .port = port,
                                          .user = usr, .pass = pwd, .recipient = rcp };
                        smtp_send(&sc, "Contact", "Port CLOSED");
                    }
                }
#endif

#if defined SWITCH_ON_CONTACT
                relay_set(false);
                s_cooldown = 10;
#endif
            }

#if defined TOGGLE_RELAYS_SWITCH
            if (state == 0) {
                ESP_LOGI(TAG, "Toggle relay");
                relay_set(!relay_get());
            }
#endif
        }
    }
}

void portstate_init(void)
{
    gpio_reset_pin(ACTIVE_PORT_GPIO);

    gpio_config_t conf = {
        .pin_bit_mask = 1ULL << ACTIVE_PORT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&conf);

    xTaskCreate(portstate_task, "portstate", 6144, NULL, 2, NULL);
    ESP_LOGI(TAG, "Portstate init (GPIO%d)", ACTIVE_PORT_GPIO);
}

#endif /* PORTSTATE_ENABLED || RX_PORTSTATE_ENABLED */
