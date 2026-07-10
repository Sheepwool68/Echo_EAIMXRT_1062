/*
 * time_sync.h
 *
 * Pure decision logic extracted from the main loop's DS3231/GPS
 * handling in ActiveRFID.C (the ds_rollover and pps/old_pps blocks).
 *
 * As with every other module in this port: no I2C/GPIO/display calls
 * here. This tells you WHAT to do; your task/main-loop glue does the
 * actual RTC_Get()-equivalent I2C read, GENIE display writes, and
 * comms_NRF(0x08) SPI call.
 *
 * FLAGGED SIMPLIFICATION vs. the original:
 * The original updates iDSTimeFromRabbit (the ms-timestamp used for
 * within-second offset calculations, e.g. in trigger_do()) in TWO
 * places: unconditionally every second inside DS3231_isr(), AND again
 * -- redundantly, with a slightly later/staler value -- once a minute
 * in the main loop's "if(mytime->seconds==0)" block. The ISR's copy is
 * strictly more accurate (captured at the actual hardware edge) and
 * fires every second; the main-loop copy is vestigial. This port keeps
 * only the ISR-level capture (see the RT1062 glue file) and does NOT
 * reproduce the redundant main-loop overwrite. Flagging in case there
 * was a reason for it that isn't visible from the source alone.
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include "rtc_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Which display fields need refreshing given the freshly-read time.
 * Mirrors the original's nested "only update the field above once the
 * one below rolls to zero" cadence exactly:
 *   seconds: always
 *   minutes: only when seconds == 0
 *   hours:   only when seconds == 0 AND minutes == 0
 *   date:    only when seconds == 0 AND minutes == 0 AND hours == 0
 */
typedef struct {
    int refresh_seconds;
    int refresh_minutes;
    int refresh_hours;
    int refresh_date;
} time_display_actions_t;

time_display_actions_t time_sync_display_refresh_actions(const rtc_datetime_t *dt);

/*
 * GPS PPS edge detection (was `if(pps != old_pps)`).
 * Call with the current and previous sampled PPS line states; returns
 * non-zero exactly once per transition, same as the original's toggle
 * comparison.
 */
int pps_edge_detected(int pps_current, int pps_previous);

/*
 * Gating for whether a detected PPS edge should trigger a GPS time
 * sync (was `if(!set_time && Settings.AutoSetGPSTime)`). Only true the
 * very first time a GPS fix is available, if auto-sync is enabled --
 * after that, ongoing sync happens via the DS3231's own 1Hz tick, not
 * repeated GPS syncs on every PPS edge.
 */
int pps_should_trigger_gps_sync(int already_time_set, int auto_set_gps_time_enabled);

/*
 * Retry-until-SPI-free gating for pushing the current time to the
 * nRF52833 (was the `if(set_time_nrf){ if(!BitRdPortI(PBDR,3)){...} }`
 * block). *pending mirrors the original's set_time_nrf variable:
 * caller sets it to 1 whenever a new time has been set (GPS sync,
 * manual PC time-set, timezone change) and calls this once per
 * rollover tick until it returns 1 (at which point *pending has been
 * cleared and the caller should actually call the nRF SPI push).
 *
 * Returns 1 if the caller should push time to the nRF now, 0 if not
 * (either nothing pending, or SPI bus still busy -- will retry next tick).
 */
int time_sync_maybe_push_nrf_time(int *pending, int spi_bus_busy);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SYNC_H */
