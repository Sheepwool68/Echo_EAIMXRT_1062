/*
 * rtc_time.h
 *
 * DS3231-specific conversions built on civil_time.h: BCD<->binary,
 * the DS3231's day-of-week register convention, and the nRF52833
 * time-sync epoch encoding.
 *
 * Ported from the RTC_Get()/RTC_Set()/mktime()/mktm() calls scattered
 * through ActiveRFID.C -- those were Rabbit BIOS/library calls that
 * hid the actual DS3231 register format from the application code.
 * On the RT1062 you're writing the I2C driver yourself, so the BCD
 * conversion (a classic source of RTC driver bugs) needs to be
 * explicit and tested, which it wasn't visible to test before.
 */

#ifndef RTC_TIME_H
#define RTC_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Plain calendar datetime. wday: 0=Sunday..6=Saturday (standard tm_wday
 * convention) -- NOT the DS3231's native 1-7 register convention; see
 * ds3231_regs_to_datetime()/ds3231_datetime_to_regs() for that mapping. */
typedef struct {
    int year;  /* full 4-digit year, e.g. 2024 */
    int mon;   /* 1-12 */
    int mday;  /* 1-31 */
    int hour;  /* 0-23 (24-hour mode only -- see ds3231_regs_t note) */
    int min;   /* 0-59 */
    int sec;   /* 0-59 */
    int wday;  /* 0-6, 0=Sunday */
} rtc_datetime_t;

/* ---- BCD conversion (DS3231 registers are BCD-encoded) ------------- */
uint8_t rtc_bcd_to_bin(uint8_t bcd);
uint8_t rtc_bin_to_bcd(uint8_t bin);

/*
 * Raw DS3231 register image (7 consecutive registers 0x00-0x06), all
 * BCD-encoded as the chip stores them.
 *
 * ASSUMES 24-HOUR MODE: the DS3231's hour register has a mode bit
 * (bit 6) that switches between 12/24-hour representation. This driver
 * assumes 24-hour mode throughout, matching the original firmware's
 * behaviour (hours used directly as 0-23 with no AM/PM handling
 * anywhere in ActiveRFID.C). Your DS3231 init code on the RT1062 MUST
 * explicitly write the hour register with bit 6 clear the first time
 * you set the clock, or leftover 12-hour-mode state from a previous
 * configuration could silently corrupt every read afterward.
 */
typedef struct {
    uint8_t sec;    /* reg 0x00 */
    uint8_t min;    /* reg 0x01 */
    uint8_t hour;   /* reg 0x02, 24hr mode, bit6=0 */
    uint8_t dow;    /* reg 0x03, 1-7 (chip's own convention, NOT 0-6) */
    uint8_t mday;   /* reg 0x04, 1-31 */
    uint8_t month;  /* reg 0x05, 1-12 (bit7 = century, ignored -- see note) */
    uint8_t year;   /* reg 0x06, 0-99, assumed to mean 2000-2099 */
} ds3231_regs_t;

/*
 * DS3231 day-of-week register convention differs from standard
 * tm_wday: the original firmware writes/reads it as 1=Sunday..7=Saturday
 * (see the "DS day 1 is Sunday, GPS is 0" comment in SendDateTime()/
 * ApplyTimeOffset()). Preserved here: ds_dow = wday + 1.
 *
 * Century bit (month register bit 7) is ignored -- year is always
 * interpreted as 2000 + regs.year. Fine until 2100; flag for whoever
 * is still maintaining this firmware then.
 */
int ds3231_regs_to_datetime(const ds3231_regs_t *regs, rtc_datetime_t *out);
void ds3231_datetime_to_regs(const rtc_datetime_t *dt, ds3231_regs_t *out);

/* ---- Whole-datetime <-> epoch-seconds, and offset application ------ */

/* Naive epoch seconds (no timezone concept -- matches how the original
 * RTC stores local wall-clock time directly). */
int64_t rtc_datetime_to_epoch(const rtc_datetime_t *dt);
rtc_datetime_t rtc_epoch_to_datetime(int64_t epoch_seconds);

/*
 * Shifts a datetime by offset_seconds and recomputes wday correctly.
 * Was ApplyTimeOffset()'s core arithmetic (offset = tz_diff_hours*3600
 * + add30_halfhour*1800), with the Rabbit-RTC I/O stripped out --
 * that's now the caller's job (write the result back via
 * ds3231_datetime_to_regs() + your I2C driver).
 */
rtc_datetime_t rtc_apply_seconds_offset(const rtc_datetime_t *dt, int64_t offset_seconds);

/*
 * ===========================================================================
 * FLAGGED / UNVERIFIED: nRF52833 time-sync epoch.
 *
 * The original's inline comment claims nrftime is "seconds since
 * 1/1/2020", but the actual code (`nrftime = mktime(&CurTime);`) never
 * applies any epoch adjustment -- it just calls the Rabbit library's
 * mktime(), which strongly suggests the real wire value is standard
 * Unix epoch seconds (1970), and the comment is stale or wrong.
 *
 * Getting this wrong would silently desync the nRF52833's clock by 50
 * years without any error being raised anywhere -- VERIFY against the
 * nRF52833 firmware source (or a logic-analyzer capture of a known-
 * good exchange with the original hardware) before trusting this.
 *
 * Defaults to 0 (i.e. standard Unix epoch, matching what the original
 * code actually does). If you confirm the nRF firmware really does
 * expect seconds-since-2020, change this to 1577836800.
 * ===========================================================================
 */
#define NRF_EPOCH_OFFSET_SECONDS 0

uint32_t rtc_datetime_to_nrf_time(const rtc_datetime_t *dt);

#ifdef __cplusplus
}
#endif

#endif /* RTC_TIME_H */
