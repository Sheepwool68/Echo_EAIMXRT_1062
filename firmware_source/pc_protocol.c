/*
 * pc_protocol.c
 *
 * See pc_protocol.h for porting rationale. No socket/board dependency;
 * compiles and unit-tests on the host.
 */

#include "pc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Command leading-byte constants, named for readability against the
 * original's raw decimal/hex literals. */
#define BYTE_REWIND_BY_TIME       56  /* '8' */
#define BYTE_REWIND_BY_RECNO      54  /* '6' */
#define BYTE_STOP_REWIND          57  /* '9' */
#define BYTE_UHF_STOP             83  /* 'S' */
#define BYTE_UHF_START            82  /* 'R' */
#define BYTE_START_LIVE_DATA      55  /* '7' */
#define BYTE_STOP_LIVE_DATA      115  /* 's' */
#define BYTE_SET_TIME            116  /* 't' */
#define BYTE_GET_TIME            114  /* 'r' */
#define BYTE_GET_READING_STATUS 0x3F  /* '?' */
#define BYTE_GET_SETTINGS         85  /* 'U' */
#define BYTE_SET_SETTINGS        117  /* 'u' */
#define BYTE_REMOTE_CONFIG      0x03
#define BYTE_BATTERY_QUERY        66  /* 'B' */
#define BYTE_SET_OUTPUT_TYPE    0x09

/* Fixed length required for the 't' (set time) command payload:
 * "t 11:50:27 08-10-2018" == 21 bytes exactly. */
#define SET_TIME_CMD_LEN 21

/* ------------------------------------------------------------------ */

int pc_validate_output_type(uint8_t value)
{
    return (value == (uint8_t)OUTPUT_DEC) || (value == (uint8_t)OUTPUT_HEX);
}

size_t pc_sanitize_command_length(const uint8_t *buf, size_t len, size_t max_len)
{
    size_t n = 0;
    while (n < len && n < max_len && buf[n] != '\0') {
        n++;
    }
    return n;
}

int pc_parse_rewind_command(const uint8_t *buf, size_t len, pc_rewind_params_t *out)
{
    size_t i;
    const char *ptr;
    char *endptr;
    long from_val, to_val;

    if (len < 4) {
        return 0; /* not even enough for type + 2 skipped chars + at least 1 digit */
    }

    if (buf[0] == (uint8_t)BYTE_REWIND_BY_TIME) {
        out->type = REWIND_BY_TIME;
    } else if (buf[0] == (uint8_t)BYTE_REWIND_BY_RECNO) {
        out->type = REWIND_BY_RECNO;
    } else {
        return 0;
    }

    /* buf[1] = split char (unused), buf[2] = antenna char (unused) */
    ptr = (const char *)&buf[3];

    from_val = strtol(ptr, &endptr, 0);
    if (endptr == ptr) {
        return 0; /* no digits found */
    }
    out->from_time = (uint32_t)from_val;

    /* Find the CR delimiter, bounded by len (original walked unbounded
     * and could run off the buffer if the client never sent a CR --
     * fixed here). */
    i = (size_t)((const uint8_t *)endptr - buf);
    while (i < len && buf[i] != '\r') {
        i++;
    }
    if (i >= len) {
        return 0; /* no CR found within the received data */
    }
    i++; /* skip the CR */
    if (i >= len) {
        out->to_time = 0;
        return 1; /* CR was the last byte; matches original's ToTime=0 default */
    }

    to_val = strtol((const char *)&buf[i], &endptr, 0);
    out->to_time = (endptr == (const char *)&buf[i]) ? 0 : (uint32_t)to_val;
    return 1;
}

int pc_parse_start_live_data_command(const uint8_t *buf, size_t len,
                                      pc_start_live_data_params_t *out)
{
    const char *ptr;
    char *endptr;
    long val;

    if (len < 3) {
        return 0;
    }
    ptr = (const char *)&buf[3];
    if (len == 3) {
        out->from_time = 0;
        return 1; /* no digits at all -- matches strtol(ptr,...) on an
                      empty/immediately-terminated string returning 0 */
    }
    val = strtol(ptr, &endptr, 0);
    out->from_time = (endptr == ptr) ? 0 : (uint32_t)val;
    return 1;
}

int pc_parse_set_datetime_command(const uint8_t *buf, size_t len, pc_datetime_fields_t *out)
{
    char tmp[SET_TIME_CMD_LEN + 1];

    if (len < SET_TIME_CMD_LEN) {
        return 0;
    }
    memcpy(tmp, buf, SET_TIME_CMD_LEN);
    tmp[SET_TIME_CMD_LEN] = '\0';

    /* Fixed offsets, matching the original SetDateTimeX() exactly:
     *  "t HH:MM:SS DD-MM-YYYY"
     *   0123456789012345678901
     *             1111111111222 */
    out->hour = atoi(&tmp[2]);
    out->min  = atoi(&tmp[5]);
    out->sec  = atoi(&tmp[8]);
    out->mday = atoi(&tmp[11]);
    out->mon  = atoi(&tmp[14]);
    out->year = atoi(&tmp[17]); /* full year, e.g. 2018 -- caller subtracts
                                    1900 if feeding a struct tm */
    return 1;
}

pc_parsed_command_t pc_classify_command(const uint8_t *buf, size_t len)
{
    pc_parsed_command_t result;
    memset(&result, 0, sizeof(result));

    if (len == 0) {
        result.id = PC_CMD_UNKNOWN;
        return result;
    }

    switch (buf[0]) {
        case BYTE_REWIND_BY_TIME:
        case BYTE_REWIND_BY_RECNO:
            if (pc_parse_rewind_command(buf, len, &result.params.rewind)) {
                result.id = PC_CMD_REWIND;
            } else {
                result.id = PC_CMD_MALFORMED;
            }
            break;

        case BYTE_STOP_REWIND:
            result.id = PC_CMD_STOP_REWIND;
            break;

        case BYTE_UHF_STOP:
            result.id = PC_CMD_UHF_STOP;
            break;

        case BYTE_UHF_START:
            result.id = PC_CMD_UHF_START;
            break;

        case BYTE_START_LIVE_DATA:
            if (pc_parse_start_live_data_command(buf, len, &result.params.start_live_data)) {
                result.id = PC_CMD_START_LIVE_DATA;
            } else {
                result.id = PC_CMD_MALFORMED;
            }
            break;

        case BYTE_STOP_LIVE_DATA:
            result.id = PC_CMD_STOP_LIVE_DATA;
            break;

        case BYTE_SET_TIME: {
            size_t sanitized_len = pc_sanitize_command_length(buf, len, SET_TIME_CMD_LEN);
            if (pc_parse_set_datetime_command(buf, sanitized_len, &result.params.datetime)) {
                result.id = PC_CMD_SET_TIME;
            } else {
                result.id = PC_CMD_MALFORMED;
            }
            break;
        }

        case BYTE_GET_TIME:
            result.id = PC_CMD_GET_TIME;
            break;

        case BYTE_GET_READING_STATUS:
            result.id = PC_CMD_GET_READING_STATUS;
            break;

        case BYTE_GET_SETTINGS:
            result.id = PC_CMD_GET_SETTINGS;
            break;

        case BYTE_SET_SETTINGS:
            result.id = PC_CMD_SET_SETTINGS;
            break;

        case BYTE_REMOTE_CONFIG:
            result.id = PC_CMD_REMOTE_CONFIG;
            break;

        case BYTE_BATTERY_QUERY:
            result.id = PC_CMD_BATTERY_QUERY;
            break;

        case BYTE_SET_OUTPUT_TYPE:
            if (len >= 2 && pc_validate_output_type(buf[1])) {
                result.id = PC_CMD_SET_OUTPUT_TYPE;
                result.params.output_type = buf[1];
            } else {
                result.id = PC_CMD_MALFORMED;
            }
            break;

        default:
            result.id = PC_CMD_UNKNOWN;
            break;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* Outgoing message formatters                                         */
/* ------------------------------------------------------------------ */

void pc_build_reading_status(int is_reading, int send_data_active, uint8_t out[5])
{
    out[0] = 0x53; /* 'S' */
    out[1] = 0x3D; /* '=' */
    out[2] = is_reading ? 0x31 : 0x30;
    out[3] = send_data_active ? 0x31 : 0x30;
    out[4] = 0x0A;
}

int pc_format_datetime_reply(const pc_datetime_fields_t *t, long raw_seconds,
                              char *out, size_t out_size)
{
    return snprintf(out, out_size, "%.2d:%.2d:%.2d %.2d-%.2d-%d (%ld)\r\n",
                     t->hour, t->min, t->sec, t->mday, t->mon, t->year, raw_seconds);
}

int pc_format_battery_status(int batt_percent, char *out, size_t out_size)
{
    return snprintf(out, out_size, "V=%d\n", batt_percent);
}

int pc_build_connect_greeting(unsigned long last_time_sent, char *out, size_t out_size)
{
    return snprintf(out, out_size, "Connected,%lu,U\n", last_time_sent);
}

int pc_format_setting_int(uint8_t setting_id, int value, char *out, size_t out_size)
{
    return snprintf(out, out_size, "U%c%d\n", (char)setting_id, value);
}

int pc_format_setting_long(uint8_t setting_id, unsigned long value, char *out, size_t out_size)
{
    return snprintf(out, out_size, "U%c%lu\n", (char)setting_id, value);
}

int pc_format_setting_str(uint8_t setting_id, const char *value, char *out, size_t out_size)
{
    return snprintf(out, out_size, "U%c%s\n", (char)setting_id, value);
}

int pc_build_discovery_reply(const uint8_t mac[6], uint8_t *out, size_t out_size, size_t *out_len)
{
    static const uint8_t prefix[8] = {0x01, 0x09, 0x1E, 0x00, 0xDC, 0x05, 0x00, 0x00};
    static const char name[] = "Ultra (Echo)"; /* 12 chars, no NUL written to packet */
    char mac_str[18]; /* "xx:xx:xx:xx:xx:xx" = 17 chars + NUL, used as scratch only */
    size_t name_len = sizeof(name) - 1; /* 12 */
    size_t mac_len;

    /* Total packet layout, byte-for-byte matching the original:
     *   [0..7]   8-byte prefix
     *   [8..19]  "Ultra (Echo)" (12 bytes, NOT NUL-terminated in the packet)
     *   [20]     0x00 (was the sprintf NUL terminator in the original --
     *             kept as an explicit zero byte for wire compatibility
     *             with any client parsing this as a C string)
     *   [21..37] "xx:xx:xx:xx:xx:xx" (17 bytes)
     * Total = 8 + 12 + 1 + 17 = 38 bytes. out_size must be >= 38. */
    if (out_size < 38) {
        return 0;
    }

    snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    mac_len = strlen(mac_str); /* expected 17 */

    memcpy(&out[0], prefix, 8);
    memcpy(&out[8], name, name_len);
    out[8 + name_len] = 0x00; /* the byte the original's NUL terminator occupied,
                                  placed intentionally rather than as an
                                  out-of-bounds side effect */
    memcpy(&out[8 + name_len + 1], mac_str, mac_len);

    *out_len = 8 + name_len + 1 + mac_len;
    return 1;
}
