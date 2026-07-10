#include "civil_time.h"

int civil_is_leap_year(int y)
{
    return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0);
}

int64_t civil_days_from_ymd(int y, int m, int d)
{
    int64_t yy = y - (m <= 2 ? 1 : 0);
    int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
    int64_t yoe = yy - era * 400;                          /* [0, 399] */
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; /* [0, 365] */
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;    /* [0, 146096] */
    return era * 146097 + doe - 719468;
}

void civil_ymd_from_days(int64_t z, int *out_y, int *out_m, int *out_d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                          /* [0, 146096] */
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0, 399] */
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);   /* [0, 365] */
    int64_t mp = (5 * doy + 2) / 153;                        /* [0, 11] */
    int64_t d = doy - (153 * mp + 2) / 5 + 1;                /* [1, 31] */
    int64_t m = mp + (mp < 10 ? 3 : -9);                     /* [1, 12] */

    *out_y = (int)(y + (m <= 2 ? 1 : 0));
    *out_m = (int)m;
    *out_d = (int)d;
}

int civil_weekday_from_days(int64_t z)
{
    /* 1970-01-01 (z=0) was a Thursday. Offset by 4 so 0=Sunday, with a
     * floor-mod to handle negative z correctly. */
    int64_t wd = (z + 4) % 7;
    if (wd < 0) {
        wd += 7;
    }
    return (int)wd;
}

int64_t civil_ymdhms_to_epoch(int y, int mon, int mday, int hour, int min, int sec)
{
    int64_t days = civil_days_from_ymd(y, mon, mday);
    return days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;
}

void civil_epoch_to_ymdhms(int64_t epoch, int *out_y, int *out_mon, int *out_mday,
                            int *out_hour, int *out_min, int *out_sec, int *out_wday)
{
    int64_t days = epoch / 86400;
    int64_t rem = epoch % 86400;
    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }

    civil_ymd_from_days(days, out_y, out_mon, out_mday);
    *out_hour = (int)(rem / 3600);
    *out_min = (int)((rem % 3600) / 60);
    *out_sec = (int)(rem % 60);
    *out_wday = civil_weekday_from_days(days);
}
