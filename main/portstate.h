#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined RX_PORTSTATE_ENABLED
#define RX_PORTSTATE_GPIO GPIO_NUM_3    /* RXD on ESP32 */
#elif defined PORTSTATE_ENABLED
#define PORTSTATE_GPIO GPIO_NUM_0
#endif

void portstate_init(void);
int portstate_read(void);

#ifdef __cplusplus
}
#endif