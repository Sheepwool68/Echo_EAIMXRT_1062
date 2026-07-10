#include <stdio.h>
#include <assert.h>
#include "rtc_time.h"

static void test_bcd_roundtrip(void) {
    for (int i = 0; i <= 59; i++) {
        uint8_t bcd = rtc_bin_to_bcd((uint8_t)i);
        uint8_t back = rtc_bcd_to_bin(bcd);
        assert(back == i);
    }
    /* spot check known encodings */
    assert(rtc_bin_to_bcd(0) == 0x00);
    assert(rtc_bin_to_bcd(9) == 0x09);
    assert(rtc_bin_to_bcd(10) == 0x10);
    assert(rtc_bin_to_bcd(59) == 0x59);
    assert(rtc_bcd_to_bin(0x59) == 59);
    printf("test_bcd_roundtrip OK\n");
}

static void test_ds3231_regs_to_datetime(void) {
    /* 2024-07-03 14:05:09, Wednesday.
     * 2024-07-03 was in fact a Wednesday -- standard tm_wday = 3.
     * DS3231 dow register (1=Sunday..7=Saturday) for Wednesday = 4. */
    ds3231_regs_t regs;
    regs.sec = 0x09;
    regs.min = 0x05;
    regs.hour = 0x14; /* 24hr mode, BCD 14 */
    regs.dow = 0x04;  /* Wednesday in DS3231's 1-7/Sunday=1 convention */
    regs.mday = 0x03;
    regs.month = 0x07;
    regs.year = 0x24;

    rtc_datetime_t dt;
    int ok = ds3231_regs_to_datetime(&regs, &dt);
    assert(ok);
    assert(dt.year == 2024);
    assert(dt.mon == 7);
    assert(dt.mday == 3);
    assert(dt.hour == 14);
    assert(dt.min == 5);
    assert(dt.sec == 9);
    assert(dt.wday == 3); /* standard convention: Wednesday = 3 */

    printf("test_ds3231_regs_to_datetime OK\n");
}

static void test_ds3231_datetime_to_regs_roundtrip(void) {
    rtc_datetime_t dt = { 2024, 7, 3, 14, 5, 9, 3 };
    ds3231_regs_t regs;
    ds3231_datetime_to_regs(&dt, &regs);

    assert(regs.sec == 0x09);
    assert(regs.min == 0x05);
    assert(regs.hour == 0x14);
    assert(regs.dow == 0x04); /* Wednesday -> DS3231 convention 4 */
    assert(regs.mday == 0x03);
    assert(regs.month == 0x07);
    assert(regs.year == 0x24);

    /* round trip back */
    rtc_datetime_t back;
    int ok = ds3231_regs_to_datetime(&regs, &back);
    assert(ok);
    assert(back.year == dt.year && back.mon == dt.mon && back.mday == dt.mday);
    assert(back.hour == dt.hour && back.min == dt.min && back.sec == dt.sec);
    assert(back.wday == dt.wday);

    printf("test_ds3231_datetime_to_regs_roundtrip OK\n");
}

static void test_invalid_dow_rejected(void) {
    ds3231_regs_t regs = {0};
    regs.dow = 0x00; /* invalid: DS3231 dow is 1-7, never 0 */
    rtc_datetime_t dt;
    int ok = ds3231_regs_to_datetime(&regs, &dt);
    assert(!ok);

    regs.dow = 0x08; /* invalid: out of range */
    ok = ds3231_regs_to_datetime(&regs, &dt);
    assert(!ok);

    printf("test_invalid_dow_rejected OK\n");
}

static void test_timezone_offset_application(void) {
    /* Perth (original firmware default) is UTC+8. Simulate a timezone
     * change from +8 to +9 (offset = +1 hour = 3600s), matching
     * ApplyTimeOffset()'s offset formula. */
    rtc_datetime_t dt = { 2024, 7, 3, 23, 30, 0, 3 }; /* Wed 23:30 */
    rtc_datetime_t shifted = rtc_apply_seconds_offset(&dt, 3600);

    assert(shifted.year == 2024 && shifted.mon == 7 && shifted.mday == 4);
    assert(shifted.hour == 0 && shifted.min == 30);
    assert(shifted.wday == 4); /* rolled into Thursday */

    printf("test_timezone_offset_application OK\n");
}

static void test_add30_half_hour_offset(void) {
    /* Settings.Add30 contributes 1800 seconds, matching
     * (time_offset*3600) + (Settings.Add30 * 1800) in the original. */
    rtc_datetime_t dt = { 2024, 7, 3, 10, 45, 0, 3 };
    rtc_datetime_t shifted = rtc_apply_seconds_offset(&dt, 1800);
    assert(shifted.hour == 11 && shifted.min == 15);
    printf("test_add30_half_hour_offset OK\n");
}

static void test_nrf_time_default_epoch(void) {
    /* With NRF_EPOCH_OFFSET_SECONDS == 0 (default / current best
     * understanding), rtc_datetime_to_nrf_time should exactly match
     * the raw Unix epoch value. */
    rtc_datetime_t dt = { 2024, 1, 1, 0, 0, 0, 1 };
    uint32_t nrf_time = rtc_datetime_to_nrf_time(&dt);
    assert(nrf_time == 1704067200UL);
    printf("test_nrf_time_default_epoch OK (nrf_time=%u)\n", nrf_time);
}

int main(void) {
    test_bcd_roundtrip();
    test_ds3231_regs_to_datetime();
    test_ds3231_datetime_to_regs_roundtrip();
    test_invalid_dow_rejected();
    test_timezone_offset_application();
    test_add30_half_hour_offset();
    test_nrf_time_default_epoch();
    printf("\nAll rtc_time tests passed.\n");
    return 0;
}
