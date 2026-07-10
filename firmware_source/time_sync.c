#include "time_sync.h"

time_display_actions_t time_sync_display_refresh_actions(const rtc_datetime_t *dt)
{
    time_display_actions_t a;
    a.refresh_seconds = 1;
    a.refresh_minutes = 0;
    a.refresh_hours = 0;
    a.refresh_date = 0;

    if (dt->sec == 0) {
        a.refresh_minutes = 1;
        if (dt->min == 0) {
            a.refresh_hours = 1;
            if (dt->hour == 0) {
                a.refresh_date = 1;
            }
        }
    }
    return a;
}

int pps_edge_detected(int pps_current, int pps_previous)
{
    return pps_current != pps_previous;
}

int pps_should_trigger_gps_sync(int already_time_set, int auto_set_gps_time_enabled)
{
    return (!already_time_set) && auto_set_gps_time_enabled;
}

int time_sync_maybe_push_nrf_time(int *pending, int spi_bus_busy)
{
    if (!*pending) {
        return 0;
    }
    if (spi_bus_busy) {
        return 0; /* leave pending set; caller retries next tick */
    }
    *pending = 0;
    return 1;
}
