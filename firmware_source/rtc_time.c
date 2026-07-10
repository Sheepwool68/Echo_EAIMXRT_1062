#include "rtc_time.h"
#include "civil_time.h"

uint8_t rtc_bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

uint8_t rtc_bin_to_bcd(uint8_t bin)
{
    return (uint8_t)((((bin / 10) & 0x0F) << 4) | (bin % 10));
}

int ds3231_regs_to_datetime(const ds3231_regs_t *regs, rtc_datetime_t *out)
{
    uint8_t dow = rtc_bcd_to_bin(regs->dow);
    if (dow < 1 || dow > 7) {
        return 0; /* invalid register content -- caller should treat as a read error */
    }

    out->sec  = rtc_bcd_to_bin(regs->sec);
    out->min  = rtc_bcd_to_bin(regs->min);
    out->hour = rtc_bcd_to_bin((uint8_t)(regs->hour & 0x3F)); /* strip mode/20-hr bits, 24h mode assumed */
    out->mday = rtc_bcd_to_bin(regs->mday);
    out->mon  = rtc_bcd_to_bin((uint8_t)(regs->month & 0x1F)); /* strip century bit */
    out->year = 2000 + rtc_bcd_to_bin(regs->year);
    out->wday = dow - 1; /* DS3231 1-7 (1=Sunday) -> standard 0-6 (0=Sunday) */
    return 1;
}

void ds3231_datetime_to_regs(const rtc_datetime_t *dt, ds3231_regs_t *out)
{
    int year_2digit = dt->year - 2000;
    if (year_2digit < 0) year_2digit = 0;
    if (year_2digit > 99) year_2digit = 99;

    out->sec   = rtc_bin_to_bcd((uint8_t)dt->sec);
    out->min   = rtc_bin_to_bcd((uint8_t)dt->min);
    out->hour  = rtc_bin_to_bcd((uint8_t)dt->hour); /* bit6=0 -> 24hr mode */
    out->dow   = rtc_bin_to_bcd((uint8_t)(dt->wday + 1)); /* standard 0-6 -> DS3231 1-7 */
    out->mday  = rtc_bin_to_bcd((uint8_t)dt->mday);
    out->month = rtc_bin_to_bcd((uint8_t)dt->mon); /* century bit left clear */
    out->year  = rtc_bin_to_bcd((uint8_t)year_2digit);
}

int64_t rtc_datetime_to_epoch(const rtc_datetime_t *dt)
{
    return civil_ymdhms_to_epoch(dt->year, dt->mon, dt->mday, dt->hour, dt->min, dt->sec);
}

rtc_datetime_t rtc_epoch_to_datetime(int64_t epoch_seconds)
{
    rtc_datetime_t dt;
    civil_epoch_to_ymdhms(epoch_seconds, &dt.year, &dt.mon, &dt.mday,
                           &dt.hour, &dt.min, &dt.sec, &dt.wday);
    return dt;
}

rtc_datetime_t rtc_apply_seconds_offset(const rtc_datetime_t *dt, int64_t offset_seconds)
{
    int64_t e = rtc_datetime_to_epoch(dt);
    return rtc_epoch_to_datetime(e + offset_seconds);
}

uint32_t rtc_datetime_to_nrf_time(const rtc_datetime_t *dt)
{
    int64_t e = rtc_datetime_to_epoch(dt) - NRF_EPOCH_OFFSET_SECONDS;
    return (uint32_t)e; /* wraps in 2106 relative to whichever epoch is configured; not a concern for this device's service life */
}
