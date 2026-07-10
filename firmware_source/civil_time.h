/*
 * civil_time.h
 *
 * Pure, dependency-free civil-calendar <-> epoch-seconds conversion.
 * Not part of the original firmware -- this is new foundational code
 * the RTC/GPS port is built on, because getting calendar arithmetic
 * subtly wrong (leap years, day-of-week) is a classic source of bugs
 * that stay invisible until they silently corrupt timestamps months
 * later. Uses the well-known algorithm from Howard Hinnant's
 * "chrono-Compatible Low-Level Date Algorithms" (public domain,
 * http://howardhinnant.github.io/date_algorithms.html), which is
 * correct for the full proleptic Gregorian calendar including
 * negative years -- far more range than we need here, but it costs
 * nothing and removes any doubt about leap-year edge cases.
 *
 * Deliberately does NOT use the standard library's mktime()/gmtime():
 * those are timezone/locale-dependent in ways that vary across libc
 * implementations (including newlib on embedded targets), and the
 * original firmware's RTC stores local wall-clock time directly with
 * no timezone concept -- we want pure, deterministic arithmetic with
 * no hidden locale state.
 */

#ifndef CIVIL_TIME_H
#define CIVIL_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Days since 1970-01-01 for the given proleptic-Gregorian date.
 * m is 1-12, d is 1-31 (day of month).
 */
int64_t civil_days_from_ymd(int y, int m, int d);

/* Inverse of the above: decomposes days-since-epoch back into y/m/d. */
void civil_ymd_from_days(int64_t days, int *out_y, int *out_m, int *out_d);

/* Day of week for a given days-since-epoch value: 0=Sunday..6=Saturday. */
int civil_weekday_from_days(int64_t days);

/* True if y is a leap year in the proleptic Gregorian calendar. */
int civil_is_leap_year(int y);

/*
 * Full datetime <-> epoch-seconds conversion (naive, no timezone --
 * treats the fields as a plain count of seconds since 1970-01-01
 * 00:00:00 with no DST/offset applied, matching how the original RTC
 * hardware stores whatever wall-clock value it's given).
 */
int64_t civil_ymdhms_to_epoch(int y, int mon, int mday, int hour, int min, int sec);
void civil_epoch_to_ymdhms(int64_t epoch, int *out_y, int *out_mon, int *out_mday,
                            int *out_hour, int *out_min, int *out_sec, int *out_wday);

#ifdef __cplusplus
}
#endif

#endif /* CIVIL_TIME_H */
