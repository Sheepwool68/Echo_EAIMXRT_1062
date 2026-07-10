#include "uhf_commands.h"
#include "uhf_crc.h"
#include <string.h>

uint8_t uhf_sub_checksum(const uint8_t *data, size_t len)
{
    unsigned int sum = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xFF);
}

size_t uhf_build_get_version(uint8_t *out, size_t out_size)
{
    if (out_size < 5) return 0;
    out[0] = 0xff; out[1] = 0x00; out[2] = 0x03; out[3] = 0; out[4] = 0;
    uhf_add_crc(out, 3);
    return 5;
}

size_t uhf_build_dc_check(uint8_t *out, size_t out_size)
{
    static const uint8_t cmd[6] = {0xff, 0x01, 0x61, 0x05, 0xbd, 0xb8};
    if (out_size < 6) return 0;
    memcpy(out, cmd, 6);
    return 6;
}

size_t uhf_build_return_loss_test(uint8_t *out, size_t out_size,
                                   uhf_region_t region, uint8_t antenna)
{
    static const uint8_t prefix[15] = {
        0xFF, 0x16, 0xAA, 'M', 'o', 'd', 'u', 'l', 'e', 't', 'e', 'c', 'h', 0xAA, 0x4A
    };
    if (out_size < 27) return 0;
    memcpy(out, prefix, 15);

    if (region == UHF_REGION_EU) {
        out[15] = 0x07; out[16] = 0xD0; out[17] = antenna; out[18] = 0x08; out[19] = 0x01;
        out[20] = 0x0D; out[21] = 0x3A; out[22] = 0x54; out[23] = (uint8_t)(0x6F + antenna);
        out[24] = 0xBB;
    } else {
        out[15] = 0x07; out[16] = 0xD0; out[17] = antenna; out[18] = 0x01; out[19] = 0x01;
        out[20] = 0x0D; out[21] = 0xF7; out[22] = 0x32; out[23] = (uint8_t)(0x03 + antenna);
        out[24] = 0xBB;
    }

    uhf_add_crc(out, 25);
    return 27;
}

size_t uhf_build_antenna_enable(uint8_t *out, size_t out_size,
                                 uint8_t ants_mask, uint32_t *out_duty_cycle)
{
    uint8_t pos, k, cnt, j;
    size_t total_len;

    if (ants_mask == 0) {
        if (out_duty_cycle != NULL) {
            *out_duty_cycle = 0;
        }
        return 0; /* matches the original's `if(ants)` guard: nothing sent */
    }
    if (out_size < 14) {
        return 0; /* worst case: all 4 antennas -> 14 bytes total */
    }

    out[0] = 0xff; out[1] = 0x09; out[2] = 0x91; out[3] = 0x02;
    out[4] = 0; out[5] = 0; out[6] = 0; out[7] = 0;
    out[8] = 0; out[9] = 0; out[10] = 0; out[11] = 0;

    pos = 4;
    k = 8;
    cnt = 0;
    for (j = 1; j < 5; j++) {
        if (k & ants_mask) {
            out[pos] = j;
            out[pos + 1] = j; /* monostatic ports: same value for RX and TX */
            pos = (uint8_t)(pos + 2);
            cnt++;
        }
        k = (uint8_t)(k >> 1);
    }

    if (out_duty_cycle != NULL) {
        *out_duty_cycle = (uint32_t)cnt * UHF_DWELL_TIME_MS + 10;
    }

    out[1] = (uint8_t)((cnt * 2) + 1);
    total_len = (size_t)out[1] + 3;
    uhf_add_crc(out, total_len);
    return total_len + 2;
}

size_t uhf_build_power_set(uint8_t *out, size_t out_size, uhf_power_level_t power)
{
    uint8_t hi = (uint8_t)((power >> 8) & 0xFF);
    uint8_t lo = (uint8_t)(power & 0xFF);

    if (out_size < 26) return 0;

    out[0] = 0xff; out[1] = 0x15; out[2] = 0x91; out[3] = 0x03;
    out[4] = 0x01; out[5] = hi; out[6] = lo; out[7] = hi; out[8] = lo;
    out[9] = 0x02; out[10] = hi; out[11] = lo; out[12] = hi; out[13] = lo;
    out[14] = 0x03; out[15] = hi; out[16] = lo; out[17] = hi; out[18] = lo;
    out[19] = 0x04; out[20] = hi; out[21] = lo; out[22] = hi; out[23] = lo;

    uhf_add_crc(out, 24);
    return 26;
}

size_t uhf_build_get_program(uint8_t *out, size_t out_size)
{
    static const uint8_t cmd[5] = {0xff, 0x00, 0x0c, 0x1d, 0x03};
    if (out_size < 5) return 0;
    memcpy(out, cmd, 5);
    return 5;
}

size_t uhf_build_firmware_boot_mode(uint8_t *out, size_t out_size)
{
    static const uint8_t cmd[5] = {0xff, 0x00, 0x04, 0x1d, 0x0b};
    if (out_size < 5) return 0;
    memcpy(out, cmd, 5);
    return 5;
}

size_t uhf_build_get_regions(uint8_t *out, size_t out_size)
{
    static const uint8_t cmd[5] = {0xff, 0x00, 0x71, 0x1d, 0x7e};
    if (out_size < 5) return 0;
    memcpy(out, cmd, 5);
    return 5;
}

size_t uhf_build_region_frequency(uint8_t *out, size_t out_size,
                                   uhf_region_t region, uint8_t channel)
{
    switch (region) {
    case UHF_REGION_EU:
        if (channel > 7) {
            if (out_size < 13) return 0;
            if (channel == 8 || channel == 10 || channel == 12 || channel == 14) {
                static const uint8_t a[11] = {
                    0xff, 0x28, 0x95, 0x00, 0x0D, 0x35, 0xA4, 0x00, 0x0D, 0x37, 0xFC
                };
                memcpy(out, a, 11);
            } else {
                static const uint8_t b[11] = {
                    0xff, 0x28, 0x95, 0x00, 0x0D, 0x3A, 0x54, 0x00, 0x0D, 0x3C, 0xAC
                };
                memcpy(out, b, 11);
            }
            uhf_add_crc(out, 11);
            return 13;
        } else {
            if (out_size < 6) return 0;
            out[0] = 0xff; out[1] = 0x01; out[2] = 0x97; out[3] = 0x08;
            uhf_add_crc(out, 4);
            return 6;
        }

    case UHF_REGION_AU: {
        static const uint8_t a[43] = {
            0xff, 0x28, 0x95,
            0x00, 0x0E, 0x0A, 0xBA,  0x00, 0x0E, 0x0C, 0xAE,  0x00, 0x0E, 0x0E, 0xA2,
            0x00, 0x0E, 0x10, 0x96,  0x00, 0x0E, 0x12, 0x8A,  0x00, 0x0E, 0x14, 0x7E,
            0x00, 0x0E, 0x16, 0x72,  0x00, 0x0E, 0x18, 0x66,  0x00, 0x0E, 0x1A, 0x5A,
            0x00, 0x0E, 0x1C, 0x4E
        };
        if (out_size < 45) return 0;
        memcpy(out, a, 43);
        uhf_add_crc(out, 43);
        return 45;
    }

    case UHF_REGION_CHINA:
        if (out_size < 6) return 0;
        out[0] = 0xff; out[1] = 0x01; out[2] = 0x97; out[3] = 0x06;
        uhf_add_crc(out, 4);
        return 6;

    case UHF_REGION_EU_HIGH:
        if (out_size < 6) return 0;
        out[0] = 0xff; out[1] = 0x01; out[2] = 0x97; out[3] = 0x0C;
        uhf_add_crc(out, 4);
        return 6;

    case UHF_REGION_INDONESIA: {
        static const uint8_t a[19] = {
            0xff, 0x28, 0x95,
            0x00, 0x0D, 0xFF, 0x02,  0x00, 0x0E, 0x0C, 0xAE,
            0x00, 0x0E, 0x0E, 0xA2,  0x00, 0x0E, 0x10, 0x96
        };
        if (out_size < 21) return 0;
        memcpy(out, a, 19);
        uhf_add_crc(out, 19);
        return 21;
    }

    case UHF_REGION_FCC:
    default:
        if (out_size < 6) return 0;
        out[0] = 0xff; out[1] = 0x01; out[2] = 0x97; out[3] = 0x01;
        uhf_add_crc(out, 4);
        return 6;
    }
}

size_t uhf_build_set_dynamic_q(uint8_t *out, size_t out_size)
{
    static const uint8_t cmd[8] = {0xff, 0x04, 0x9B, 0x05, 0x12, 0x00, 0xce, 0xe8};
    if (out_size < 8) return 0;
    memcpy(out, cmd, 8);
    return 8;
}

size_t uhf_build_mode_sequence(uint8_t *out, size_t out_size, int uhf_mode,
                                size_t out_command_lengths[3],
                                int *out_command_count)
{
    size_t total = 0;
    const size_t need = 9 + 8 + 8;

    if (out_size < need) return 0;

    /* Command 1: target mode -- identical in both branches in the
     * original (single target A; the dual-target alternative is
     * commented out in both). */
    out[0] = 0xff; out[1] = 0x04; out[2] = 0x9B; out[3] = 0x05;
    out[4] = 0x01; out[5] = 0x01; out[6] = 0x00;
    uhf_add_crc(out, 7);
    if (out_command_lengths != NULL) out_command_lengths[0] = 9;
    total += 9;

    /* Command 2: session (0 for finish-line/uhf_mode!=0, 1 for start-line) */
    {
        uint8_t *p = out + total;
        p[0] = 0xff; p[1] = 0x04; p[2] = 0x9B; p[3] = 0x05; p[4] = 0x00;
        p[5] = uhf_mode ? 0x00 : 0x01;
        uhf_add_crc(p, 6);
        if (out_command_lengths != NULL) out_command_lengths[1] = 8;
        total += 8;
    }

    /* Command 3: RF mode (Mode 7 DRM-FCC for finish-line, Mode 13
     * max-sensitivity for start-line -- matching the active,
     * uncommented choices in the original) */
    {
        uint8_t *p = out + total;
        p[0] = 0xff; p[1] = 0x03; p[2] = 0x9B; p[3] = 0x05; p[4] = 0x02;
        p[5] = uhf_mode ? 0x6B : 0x71;
        uhf_add_crc(p, 6);
        if (out_command_lengths != NULL) out_command_lengths[2] = 8;
        total += 8;
    }

    if (out_command_count != NULL) {
        *out_command_count = 3;
    }
    return total;
}

size_t uhf_build_start_reading(uint8_t *out, size_t out_size, int heartbeat_enabled)
{
    static const uint8_t prefix[15] = {
        0xFF, 0x13, 0xAA, 'M', 'o', 'd', 'u', 'l', 'e', 't', 'e', 'c', 'h', 0xAA, 0x48
    };
    if (out_size < 24) return 0;

    memcpy(out, prefix, 15);
    out[15] = 0x00;
    out[16] = 0xBF;
    out[17] = 0x00;
    out[18] = (uint8_t)(heartbeat_enabled ? 0x80 : 0x20);
    out[19] = 0x03;
    out[21] = 0xBB;
    out[20] = uhf_sub_checksum(&out[13], 7); /* sums out[13..19] */

    uhf_add_crc(out, 22); /* writes out[22], out[23] */
    return 24;
}

size_t uhf_build_stop_reading(uint8_t *out, size_t out_size)
{
    static const uint8_t cmd[19] = {
        0xFF, 0x0E, 0xAA, 'M', 'o', 'd', 'u', 'l', 'e', 't', 'e', 'c', 'h', 0xAA, 0x49,
        0xF3, 0xBB, 0x03, 0x91
    };
    if (out_size < 19) return 0;
    memcpy(out, cmd, 19);
    return 19;
}

size_t uhf_build_get_temperature(uint8_t *out, size_t out_size)
{
    static const uint8_t cmd[5] = {0xFF, 0x00, 0x72, 0x1D, 0x7D};
    if (out_size < 5) return 0;
    memcpy(out, cmd, 5);
    return 5;
}
