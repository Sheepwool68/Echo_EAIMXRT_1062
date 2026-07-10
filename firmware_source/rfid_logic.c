/*
 * rfid_logic.c
 *
 * See rfid_logic.h for porting notes. This file has zero dependency on
 * Rabbit/Dynamic C libraries or NXP SDK headers -- it will compile and
 * unit test anywhere, and can be linked directly into the RT1062
 * firmware image once the FatFs/lwIP glue calls into it.
 */

#if !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE) && !defined(__NEWLIB__)
/* strtok_r is POSIX; needed for strict -std=c11 builds on glibc hosts.
 * Newlib (used by MCUXpresso/ARM builds) provides it unconditionally,
 * so this is only needed for host-side unit testing. */
#define _POSIX_C_SOURCE 200809L
#endif

#include "rfid_logic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/* CreateSockString                                                    */
/* ------------------------------------------------------------------ */

int rfid_create_sock_string(const nrf_record_t *entry,
                             int is_rewind,
                             char *out, size_t out_size,
                             uint32_t uhf_code_value,
                             int is_uhf,
                             uint8_t channel,
                             output_type_t output_type,
                             int trigger_time_active)
{
    char schip[20];
    uint8_t loop_id;

    if (trigger_time_active) {
        return snprintf(out, out_size,
            "%lu,%d,%lu,%03u,%u,%d,%d,%d,%d,"
            "%02X%02X%02X%02X%02X%02X%02X%02X,%d,%lu\n",
            (unsigned long)0,
            0,
            (unsigned long)entry->date_time,
            (unsigned)entry->ms,
            (unsigned)channel,
            0,
            0, /* is rewind */
            0,
            0,
            0, 0, 0, 0, 0, 0, 0, 0,
            0,
            (unsigned long)entry->log_id);
    }

    if (is_uhf) {
        if (output_type == OUTPUT_DEC) {
            snprintf(schip, sizeof(schip), "%lu", (unsigned long)uhf_code_value);
        } else {
            snprintf(schip, sizeof(schip), "%lX", (unsigned long)uhf_code_value);
        }
        return snprintf(out, out_size,
            "%lu,%s,%lu,%03u,%u,%d,%d,%d,%u,"
            "%02X%02X%02X%02X%02X%02X%02X%02X,%d,%lu\n",
            (unsigned long)0,
            schip,
            (unsigned long)entry->date_time,
            (unsigned)entry->ms,
            (unsigned)entry->loop_data,
            (int)entry->max_RSSI,
            is_rewind,
            1,
            (unsigned)(channel + 1),
            0, 0, 0, 0, 0, 0, 0, 0,
            0,
            (unsigned long)entry->log_id);
    }

    /* LF (Active) chip record: xpdr_code is 6 raw bytes, not NUL-terminated */
    snprintf(schip, sizeof(schip), "%.6s", entry->xpdr_code);
    loop_id = (uint8_t)(((entry->loop_data >> 6) & 0x0F) + 1);

    return snprintf(out, out_size,
        "%lu,%s,%lu,%03u,%u,%d,%d,%u,%u,"
        "%02X%02X%02X%02X%02X%02X%02X%02X,%d,%lu\n",
        (unsigned long)0,
        schip,
        (unsigned long)entry->date_time,
        (unsigned)entry->ms,
        (unsigned)loop_id,
        (int)entry->max_RSSI,
        is_rewind,
        (unsigned)entry->wake_count,
        (unsigned)entry->battery,
        0, 0, 0, 0, 0, 0, 0, 0,
        0,
        (unsigned long)entry->log_id);
}

/* ------------------------------------------------------------------ */
/* BinarySearch                                                        */
/* ------------------------------------------------------------------ */

int rfid_binary_search_log(void *file_ctx,
                            rfid_seek_fn seek_fn,
                            rfid_read_fn read_fn,
                            rfid_filesize_fn file_size_fn,
                            uint32_t start_value,
                            rewind_type_t rewind_type,
                            uint32_t *out_record_no)
{
    uint32_t first, last, pos;
    long file_size;
    nrf_record_t entry;
    int found;

    if (start_value == 0) {
        if (seek_fn(file_ctx, 0) < 0) {
            return -1;
        }
        *out_record_no = 0;
        return 1;
    }

    file_size = file_size_fn(file_ctx);
    if (file_size < 0) {
        return -1;
    }

    /* Read the last record to see if start_value is beyond the end */
    if (seek_fn(file_ctx, file_size - (long)sizeof(entry)) < 0) {
        return -1;
    }
    if (read_fn(file_ctx, &entry, sizeof(entry)) != (long)sizeof(entry)) {
        return -1;
    }

    first = 0;
    last = (uint32_t)(file_size / (long)sizeof(entry));
    found = 0;

    if ((rewind_type == REWIND_BY_TIME  && entry.date_time < start_value) ||
        (rewind_type == REWIND_BY_RECNO && entry.log_id    < start_value)) {
        /* Last record is older than what we're looking for: nothing to rewind */
        return 0;
    }

    pos = 0;
    while (first <= last && !found) {
        pos = (first + last) / 2;

        if (first == 0 && last == 0) {
            break;
        }

        if (seek_fn(file_ctx, (long)pos * (long)sizeof(entry)) < 0) {
            return -1;
        }
        if (read_fn(file_ctx, &entry, sizeof(entry)) != (long)sizeof(entry)) {
            return -1;
        }

        if ((rewind_type == REWIND_BY_TIME  && entry.date_time == start_value) ||
            (rewind_type == REWIND_BY_RECNO && entry.log_id    == start_value)) {
            found = 1;
        } else if ((rewind_type == REWIND_BY_TIME  && entry.date_time > start_value) ||
                   (rewind_type == REWIND_BY_RECNO && entry.log_id    > start_value)) {
            last = (pos > 0) ? (pos - 1) : 0;
        } else {
            first = pos + 1;
        }
    }

    *out_record_no = pos;

    /* Leave the file position at the record we found (or the closest one) */
    if (seek_fn(file_ctx, (long)pos * (long)sizeof(entry)) < 0) {
        return -1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* parseIP                                                              */
/* ------------------------------------------------------------------ */

int rfid_parse_ip(const char *ip_string, uint8_t out_ip[4])
{
    char buf[20];
    char *token, *saveptr;
    int i;
    int values[4];

    if (ip_string == NULL) {
        return 0;
    }
    if (strlen(ip_string) >= sizeof(buf)) {
        return 0;
    }
    strncpy(buf, ip_string, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    i = 0;
    token = strtok_r(buf, ".", &saveptr);
    while (token != NULL && i < 4) {
        int v = atoi(token);
        if (v < 0 || v > 255) {
            return 0;
        }
        values[i] = v;
        i++;
        token = strtok_r(NULL, ".", &saveptr);
    }

    /* Must be exactly 4 octets, and no leftover token */
    if (i != 4 || token != NULL) {
        return 0;
    }

    for (i = 0; i < 4; i++) {
        out_ip[i] = (uint8_t)values[i];
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* round_up_to_max_pow10                                               */
/* ------------------------------------------------------------------ */

int rfid_round_up_to_max_pow10(int n)
{
    int tmp;
    int i;

    tmp = n;
    i = 0;

    while ((tmp /= 10) >= 10) {
        i++;
    }

    if (n % (int)(pow(10, i + 1) + 0.5)) {
        tmp++;
    }

    for (; i >= 0; i--) {
        tmp *= 10;
    }

    return tmp;
}
