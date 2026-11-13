#include "config.h"

int config_get_debug_level(void) {
    return DEBUG_LEVEL;
}

int config_get_display_timeout_min(void) {
    return DISPLAY_TIMEOUT_MIN;
}

int config_get_display_activity_start_hour(void) {
    return DISPLAY_ACTIVITY_HOUR;
}

int config_get_display_activity_start_min(void) {
    return DISPLAY_ACTIVITY_MIN;
}

int config_get_display_activity_end_hour(void) {
    return DISPLAY_ACTIVITY_HOUR2;
}

int config_get_display_activity_end_min(void) {
    return DISPLAY_ACTIVITY_MIN2;
}

bool config_is_display_active(void) {
    /* not implemented yet – use a stub */
    return true;
}