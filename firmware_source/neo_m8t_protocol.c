#include "neo_m8t_protocol.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

void ubx_compute_checksum(uint8_t *buf, size_t length)
{
    uint8_t cka = 0, ckb = 0;
    size_t i;

    for (i = 2; i < length - 2; i++) {
        cka = (uint8_t)(cka + buf[i]);
        ckb = (uint8_t)(ckb + cka);
    }
    buf[length - 2] = cka;
    buf[length - 1] = ckb;
}

int ubx_find_sync(const uint8_t *buf, size_t buf_len)
{
    size_t i;
    if (buf_len < 2) {
        return -1;
    }
    for (i = 0; i + 1 < buf_len; i++) {
        if (buf[i] == UBX_SYNC1 && buf[i + 1] == UBX_SYNC2) {
            return (int)i;
        }
    }
    return -1;
}

int ubx_classify_ack(const uint8_t *msg_at_sync)
{
    if (msg_at_sync[3] == 0x01) {
        return 1;
    }
    if (msg_at_sync[3] == 0x00) {
        return 0;
    }
    return -1;
}

int ubx_is_nav_pvt(const uint8_t *msg_at_sync)
{
    /* Preserves the original's own incompleteness: only the length
     * field's LOW byte (offset 4) is checked against 0x5C (92
     * decimal, NAV-PVT's actual payload length); the high byte
     * (offset 5, which should be 0x00) is never checked. */
    return (msg_at_sync[2] == 0x01 && msg_at_sync[3] == 0x07 && msg_at_sync[4] == 0x5C);
}

void ubx_parse_nav_pvt(const uint8_t *s, neo_pvt_t *out)
{
    /* lon/lat: standard little-endian 32-bit reconstruction, each
     * byte treated as unsigned before shifting (the only byte that
     * should carry sign is the top one, s[33]/s[37], which is signed
     * naturally by the final 32-bit assignment). NOTE: the original's
     * Dynamic C source did `(long int)s[33] << 24` etc. directly on a
     * `char *` with no explicit unsigned cast on the lower 3 bytes --
     * if Dynamic C's `char` defaulted to signed on that platform, byte
     * values 0x80-0xFF in s[30..32]/s[34..36] would have sign-extended
     * BEFORE shifting, corrupting the reconstruction. This port uses
     * the standard/correct unsigned-byte reconstruction instead of
     * reproducing that possible defect, since (unlike the
     * operator-precedence issue elsewhere in this file, which is
     * unambiguous C grammar) this depends on a compiler default this
     * port can't verify, and "correctly reconstruct a 32-bit integer"
     * is a more fundamental correctness property than a preservable
     * quirk. Flagged here rather than assumed. */
    out->lon = (int32_t)(((uint32_t)s[33] << 24) | ((uint32_t)s[32] << 16)
                        | ((uint32_t)s[31] << 8) | (uint32_t)s[30]);
    out->lat = (int32_t)(((uint32_t)s[37] << 24) | ((uint32_t)s[36] << 16)
                        | ((uint32_t)s[35] << 8) | (uint32_t)s[34]);

    out->sats = s[29];

    /* Validity check -- see this file's header comment for the
     * preserved operator-precedence quirk. What actually executes in
     * the original is (s[17]&1) && (s[26]>0) && (s[27]&1), NOT
     * (s[17]&7)==7 as the source's phrasing suggests at a glance. */
    out->status = ((s[17] & 1) && (s[26] > 0) && (s[27] & 1)) ? 1 : 0;

    out->year = s[10] | (s[11] << 8);
    out->month = s[12];
    out->day = s[13];
    out->hours = s[14];
    out->minutes = s[15];
    out->seconds = s[16];
}

int neo_format_nmea(int32_t lat, int32_t lon, char *out, size_t out_size)
{
    int degrees, minutes;
    int32_t fractional;
    long seconds;
    char lat_dir, lon_dir;
    float minsecs;
    int n1, n2;

    if (out_size < 35) {
        return -1;
    }
    memset(out, 0, out_size);

    degrees = (int)(lat < 0 ? -(lat / 10000000) : (lat / 10000000));
    lat_dir = (lat < 0) ? 'S' : 'N';
    fractional = lat % 10000000;
    if (fractional < 0) {
        fractional = -fractional;
    }
    minsecs = (float)fractional / 10000000.0f * 60.0f;
    minutes = (int)minsecs;
    seconds = (long)((minsecs - (float)minutes) * 100000.0f);
    n1 = snprintf(out, out_size, "%02d%02d.%05ld,%c", degrees, minutes, seconds, lat_dir);
    if (n1 < 0) {
        return -1;
    }

    degrees = (int)(lon < 0 ? -(lon / 10000000) : (lon / 10000000));
    lon_dir = (lon < 0) ? 'W' : 'E';
    fractional = lon % 10000000;
    if (fractional < 0) {
        fractional = -fractional;
    }
    minsecs = (float)fractional / 10000000.0f * 60.0f;
    minutes = (int)minsecs;
    seconds = (long)((minsecs - (float)minutes) * 100000.0f);
    n2 = snprintf(out + n1, out_size - (size_t)n1, ",%03d%02d.%05ld,%c", degrees, minutes, seconds, lon_dir);
    if (n2 < 0) {
        return -1;
    }

    return n1 + n2;
}

int neo_format_dd_dms(int32_t lat, int32_t lon, char *out, size_t out_size)
{
    int degrees;
    int32_t fractional;
    char lat_dir, lon_dir;
    int n1, n2;

    if (out_size < 35) {
        return -1;
    }
    memset(out, 0, out_size);

    degrees = (int)(lat < 0 ? -(lat / 10000000) : (lat / 10000000));
    lat_dir = (lat < 0) ? 'S' : 'N';
    fractional = lat % 10000000;
    if (fractional < 0) {
        fractional = -fractional;
    }
    n1 = snprintf(out, out_size, "%d.%07ld,%c", degrees, (long)fractional, lat_dir);
    if (n1 < 0) {
        return -1;
    }

    degrees = (int)(lon < 0 ? -(lon / 10000000) : (lon / 10000000));
    lon_dir = (lon < 0) ? 'W' : 'E';
    fractional = lon % 10000000;
    if (fractional < 0) {
        fractional = -fractional;
    }
    n2 = snprintf(out + n1, out_size - (size_t)n1, ",%d.%07ld,%c", degrees, (long)fractional, lon_dir);
    if (n2 < 0) {
        return -1;
    }

    return n1 + n2;
}
