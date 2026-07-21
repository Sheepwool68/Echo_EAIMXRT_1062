/*
 * gps_stub.c
 *
 * Real implementation of gps_stub.h, backed by neo_m8t_reader.h +
 * neo_m8t_transport_rt1062.c. Uses a single module-static transport
 * instance, matching the original NEOM8T.LIB's own single-GPS,
 * global-state design (this app only ever has one GPS module).
 */

#include "gps_stub.h"
#include "neo_m8t_reader.h"
#include "neo_m8t_transport_rt1062.h"
#include "systick_ms_rt1062.h"

static neo_m8t_transport_t s_gps_transport;
static int s_gps_transport_ready;

static void ensure_transport(void)
{
    if (!s_gps_transport_ready) {
        s_gps_transport = neo_m8t_transport_rt1062_init();
        s_gps_transport_ready = 1;
    }
}

void gps_configure_timepulse(void)
{
    ensure_transport();
    neo_m8t_configure_timepulse(&s_gps_transport, systick_ms_now);
}

void gps_update_signal_status(int *out_raw_sats, int *out_status)
{
    ensure_transport();
    (void)neo_m8t_update_signal_status(&s_gps_transport, systick_ms_now, out_raw_sats, out_status);
}

int gps_sync_time_from_fix(rtc_datetime_t *out_time, int time_zone_hours, int add30)
{
    ensure_transport();
    return gps_sync_time_from_fix_impl(&s_gps_transport, out_time, time_zone_hours, add30,
                                        systick_ms_now, neo_m8t_gps_get_tick_count);
}
