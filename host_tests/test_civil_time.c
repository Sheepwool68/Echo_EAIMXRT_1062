#include <stdio.h>
#include <assert.h>
#include "civil_time.h"

static void test_known_epoch_values(void) {
    /* Well-known reference timestamps */
    assert(civil_ymdhms_to_epoch(1970, 1, 1, 0, 0, 0) == 0);
    assert(civil_ymdhms_to_epoch(2000, 1, 1, 0, 0, 0) == 946684800);
    assert(civil_ymdhms_to_epoch(2020, 1, 1, 0, 0, 0) == 1577836800);
    assert(civil_ymdhms_to_epoch(2024, 1, 1, 0, 0, 0) == 1704067200);
    printf("test_known_epoch_values OK\n");
}

static void test_known_weekdays(void) {
    int y, mon, mday, hour, min, sec, wday;

    civil_epoch_to_ymdhms(0, &y, &mon, &mday, &hour, &min, &sec, &wday);
    assert(wday == 4); /* 1970-01-01 was a Thursday */

    civil_epoch_to_ymdhms(946684800, &y, &mon, &mday, &hour, &min, &sec, &wday);
    assert(wday == 6); /* 2000-01-01 was a Saturday */

    civil_epoch_to_ymdhms(1577836800, &y, &mon, &mday, &hour, &min, &sec, &wday);
    assert(wday == 3); /* 2020-01-01 was a Wednesday */

    civil_epoch_to_ymdhms(1704067200, &y, &mon, &mday, &hour, &min, &sec, &wday);
    assert(wday == 1); /* 2024-01-01 was a Monday */

    printf("test_known_weekdays OK\n");
}

static void test_leap_day_roundtrip(void) {
    /* 2000-02-29 is a valid leap day (div by 400) */
    int64_t e = civil_ymdhms_to_epoch(2000, 2, 29, 12, 30, 45);
    int y, mon, mday, hour, min, sec, wday;
    civil_epoch_to_ymdhms(e, &y, &mon, &mday, &hour, &min, &sec, &wday);
    assert(y == 2000 && mon == 2 && mday == 29);
    assert(hour == 12 && min == 30 && sec == 45);
    assert(wday == 2); /* 2000-02-29 was a Tuesday */

    /* 1900 is NOT a leap year (div by 100, not by 400) -- Feb 1900 has 28 days */
    assert(civil_is_leap_year(2000) == 1);
    assert(civil_is_leap_year(1900) == 0);
    assert(civil_is_leap_year(2024) == 1);
    assert(civil_is_leap_year(2023) == 0);

    printf("test_leap_day_roundtrip OK\n");
}

static void test_roundtrip_many_dates(void) {
    /* Sweep a range of dates through epoch<->civil and back, confirm
     * we always get the same date out. */
    int y, mon, mday, hour, min, sec, wday;
    for (int year = 2018; year <= 2030; year++) {
        for (int m = 1; m <= 12; m++) {
            int d = 15; /* mid-month, always valid */
            int64_t e = civil_ymdhms_to_epoch(year, m, d, 3, 4, 5);
            civil_epoch_to_ymdhms(e, &y, &mon, &mday, &hour, &min, &sec, &wday);
            assert(y == year && mon == m && mday == d);
            assert(hour == 3 && min == 4 && sec == 5);
        }
    }
    printf("test_roundtrip_many_dates OK\n");
}

static void test_offset_across_month_boundary(void) {
    /* 2024-02-29 23:30:00 + 1 hour should roll into March 1st (2024 is leap) */
    int64_t e = civil_ymdhms_to_epoch(2024, 2, 29, 23, 30, 0);
    e += 3600;
    int y, mon, mday, hour, min, sec, wday;
    civil_epoch_to_ymdhms(e, &y, &mon, &mday, &hour, &min, &sec, &wday);
    assert(y == 2024 && mon == 3 && mday == 1 && hour == 0 && min == 30);
    printf("test_offset_across_month_boundary OK\n");
}

int main(void) {
    test_known_epoch_values();
    test_known_weekdays();
    test_leap_day_roundtrip();
    test_roundtrip_many_dates();
    test_offset_across_month_boundary();
    printf("\nAll civil_time tests passed.\n");
    return 0;
}
