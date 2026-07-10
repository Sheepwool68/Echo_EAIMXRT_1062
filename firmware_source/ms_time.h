/*
 * ms_time.h
 *
 * Pure, wraparound-safe millisecond elapsed-time helpers. Not part of
 * the original firmware -- new infrastructure needed because app_loop.c
 * previously hardcoded `now_ms = 0`, meaning every timer-based feature
 * (beeper-off delay, GPRS process interval, battery/GPS check
 * intervals) was silently non-functional, not just "not yet wired."
 *
 * FLAGGED FIX (not silently applied): the original's own timing checks
 * used a `now - delay > last` pattern (e.g. `MS_TIMER - BEEPDELAY >
 * LastBeepTime`), which underflows if `now < delay` -- a real window
 * during the first BEEPDELAY milliseconds after boot where the
 * comparison silently produces a huge unsigned number and evaluates
 * true when it shouldn't. This port replaces that pattern with
 * ms_has_elapsed(now, since, duration), which is safe regardless of
 * how soon after boot it's checked.
 */

#ifndef MS_TIME_H
#define MS_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Milliseconds elapsed between `since` and `now`, correct even if the
 * millisecond counter has wrapped around between the two (relies on
 * unsigned subtraction wraparound being well-defined in C, as long as
 * the true elapsed time never actually exceeds the full 32-bit range,
 * i.e. ~49.7 days -- not a concern for any interval this firmware
 * actually uses).
 */
uint32_t ms_elapsed(uint32_t now, uint32_t since);

/* True if at least `duration` ms have passed since `since`, as of `now`. */
int ms_has_elapsed(uint32_t now, uint32_t since, uint32_t duration);

#ifdef __cplusplus
}
#endif

#endif /* MS_TIME_H */
