#include "gprs_records.h"
#include <string.h>

size_t gprs_build_record(const nrf_record_t *log_entry, uint8_t channel,
                          const uint8_t mac_address[6], uint32_t record_no,
                          uint8_t *out, size_t out_size,
                          gprs_record_kind_t *out_kind)
{
    if (log_entry->xpdr_code[0] == 0x00) {
        /* UHF variant */
        gprs_rec_uhf_t rec;
        int i;
        uint32_t chip_code = 0;

        if (out_size < sizeof(rec)) {
            return 0;
        }

        for (i = 2; i <= 5; i++) {
            chip_code = (chip_code << 8) | (uint8_t)log_entry->xpdr_code[i];
        }

        rec.start_chr = 0x02;
        rec.rec_len = 28;
        rec.reader_no = 1;
        rec.record_no = record_no;
        rec.chip_code = chip_code;
        rec.seconds = log_entry->date_time;
        rec.ms = (int16_t)log_entry->ms;
        rec.mac_address[0] = mac_address[3];
        rec.mac_address[1] = mac_address[4];
        rec.mac_address[2] = mac_address[5];
        rec.ultra_id = channel; /* raw, 0-based -- see header note */
        memset(rec.reader_time, 0, sizeof(rec.reader_time));
        rec.antenna_no = (uint8_t)log_entry->loop_data; /* truncated -- see header note */
        rec.end_chr = 0x03;

        memcpy(out, &rec, sizeof(rec));
        if (out_kind != NULL) {
            *out_kind = GPRS_RECORD_UHF;
        }
        return sizeof(rec);
    } else {
        /* Active/LF variant */
        gprs_rec_active_t rec;

        if (out_size < sizeof(rec)) {
            return 0;
        }

        rec.start_chr = 0x02;
        rec.rec_len = 30;
        rec.reader_no = 1;
        rec.record_no = record_no;
        rec.ultra_id = (uint8_t)(((log_entry->loop_data >> 6) & 0x0F) + 1);
        memcpy(rec.chip_code, log_entry->xpdr_code, 6);
        rec.seconds = log_entry->date_time;
        rec.ms = (int16_t)log_entry->ms;
        rec.mac_address[0] = mac_address[3];
        rec.mac_address[1] = mac_address[4];
        rec.mac_address[2] = mac_address[5];
        memset(rec.reader_time, 0, sizeof(rec.reader_time));
        rec.antenna_no = 1; /* always 1 -- see header note */
        rec.end_chr = 0x03;

        memcpy(out, &rec, sizeof(rec));
        if (out_kind != NULL) {
            *out_kind = GPRS_RECORD_ACTIVE;
        }
        return sizeof(rec);
    }
}
