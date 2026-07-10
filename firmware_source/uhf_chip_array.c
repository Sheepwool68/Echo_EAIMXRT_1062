#include "uhf_chip_array.h"
#include <string.h>

uhf_chip_add_result_t uhf_chip_array_add(uhf_chip_entry_t *chips, size_t count,
                                          uint32_t chip_code, int rssi, uint16_t antenna,
                                          uint32_t chip_seconds, uint32_t chip_ms,
                                          int fix_rssi_update_bug,
                                          uint32_t *io_chip_count,
                                          uint32_t *io_unique_chips,
                                          size_t *out_index)
{
    size_t i;
    size_t first_zero = count; /* sentinel: count == "not found" */
    int found = 0;
    size_t found_index = 0;

    if (chip_code == 0) {
        return UHF_CHIP_IGNORED_ZERO_CODE;
    }

    for (i = 0; i < count; i++) {
        if (first_zero == count && chips[i].chip_code == 0) {
            first_zero = i;
        }
        if (chips[i].chip_code == chip_code) {
            found = 1;
            found_index = i;
            chips[i].reads++; /* unconditional, matches the original: this
                                  increments even under the RSSI-update bug */
            break;
        }
    }

    if (found) {
        int overwrite = (chips[found_index].rssi < rssi); /* new reading is stronger */
        if (fix_rssi_update_bug && overwrite) {
            chips[found_index].rssi = rssi;
            chips[found_index].antenna = antenna;
            chips[found_index].seconds = chip_seconds;
            chips[found_index].ms = (uint16_t)(chip_ms % 1000u);
        }
        if (out_index != NULL) {
            *out_index = found_index;
        }
        return UHF_CHIP_REREAD;
    }

    if (first_zero == count) {
        /* No free slot -- matches the original's silent drop (iArrayIndex
         * ends up -1, nothing is stored). */
        return UHF_CHIP_ARRAY_FULL;
    }

    chips[first_zero].chip_code = chip_code;
    chips[first_zero].rssi = rssi;
    chips[first_zero].antenna = antenna;
    chips[first_zero].seconds = chip_seconds;
    chips[first_zero].ms = (uint16_t)(chip_ms % 1000u);
    chips[first_zero].reads = 1;
    chips[first_zero].has_been_sent = 0;
    chips[first_zero].ms_timer_time = chip_ms;

    if (io_chip_count != NULL) {
        (*io_chip_count)++;
    }
    if (io_unique_chips != NULL) {
        (*io_unique_chips)++;
    }
    if (out_index != NULL) {
        *out_index = first_zero;
    }
    return UHF_CHIP_ADDED_NEW;
}

void uhf_chip_entry_to_record(const uhf_chip_entry_t *chip, nrf_record_t *out)
{
    memset(out, 0, sizeof(*out));

    out->xpdr_code[0] = 0;
    out->xpdr_code[1] = 0;
    out->xpdr_code[2] = (char)((chip->chip_code >> 24) & 0xFF);
    out->xpdr_code[3] = (char)((chip->chip_code >> 16) & 0xFF);
    out->xpdr_code[4] = (char)((chip->chip_code >> 8) & 0xFF);
    out->xpdr_code[5] = (char)(chip->chip_code & 0xFF);

    out->max_RSSI = (int8_t)(-chip->rssi); /* stored positive, matches original comment */
    out->wake_count = chip->reads;
    out->battery = 0;
    out->date_time = chip->seconds;
    out->ms = chip->ms;
    out->loop_data = chip->antenna;
    out->has_been_sent = chip->has_been_sent;
    out->log_id = 0; /* caller assigns */
}

size_t uhf_chip_array_flush_aged(uhf_chip_entry_t *chips, size_t count,
                                  uint32_t now_seconds,
                                  nrf_record_t *out_records, size_t max_out,
                                  uint32_t log_id_start)
{
    size_t produced = 0;
    size_t i;

    for (i = 0; i < count && produced < max_out; i++) {
        if (chips[i].chip_code == 0) {
            continue;
        }
        if (chips[i].seconds + UHF_CHIP_AGING_SECONDS <= now_seconds) {
            uhf_chip_entry_to_record(&chips[i], &out_records[produced]);
            out_records[produced].log_id = log_id_start + (uint32_t)produced;
            chips[i].chip_code = 0; /* free the slot */
            produced++;
        }
    }

    return produced;
}
