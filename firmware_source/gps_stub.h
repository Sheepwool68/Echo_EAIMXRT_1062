/*
 * gps_stub.h
 *
 * UPDATE: NEOM8T.LIB has now been pasted and ported (neo_m8t_protocol.h +
 * neo_m8t_reader.h) -- gps_stub.c implements every function below for
 * real now, backed by that real state machine + neo_m8t_transport_rt1062.c.
 * The name "gps_stub.h" and its function signatures are kept as the
 * stable interface app_init.c/app_loop.c already call into -- no
 * call-site changes needed.
 */

#ifndef GPS_STUB_H
#define GPS_STUB_H

#include "rtc_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Was set_UBX() -- configures the NEO-M8T's timepulse output parameters. */
void gps_configure_timepulse(void);

/* Was GetGPS_Signal() -- updates satellite-count display/status. */
void gps_update_signal_status(void);

/*
 * Was SetTime_uBlox() -- reads GPS time and calls SetDSTime() (the
 * DS3231-write half of which is now ds3231_rt1062_write() +
 * time_sync.h's offset logic). Returns 1 and fills *out_time if a fix
 * was available and time was set, 0 otherwise (was the implicit
 * `set_time` flag becoming true only on success).
 *
 * time_zone_hours/add30 were Settings.TimeZone/Settings.Add30 --
 * added as explicit parameters (the original had no equivalent
 * parameter list at all, since it read the globals directly) since
 * this stub's caller (app_loop.c's process_time_sync()) already has
 * app->settings in scope, and passing them explicitly is more
 * trustworthy than hidden module-level state that a settings-change
 * command elsewhere in the app could silently go stale against.
 */
int gps_sync_time_from_fix(rtc_datetime_t *out_time, int time_zone_hours, int add30);

#ifdef __cplusplus
}
#endif

#endif /* GPS_STUB_H */
