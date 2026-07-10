#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "gprs_records.h"

static void test_struct_sizes_match_original_rec_len(void) {
    assert(sizeof(gprs_rec_uhf_t) == 28 + 3);
    assert(sizeof(gprs_rec_active_t) == 30 + 3);
    printf("test_struct_sizes_match_original_rec_len OK (uhf=%zu, active=%zu)\n",
           sizeof(gprs_rec_uhf_t), sizeof(gprs_rec_active_t));
}

static void test_build_uhf_record(void) {
    nrf_record_t log_entry;
    memset(&log_entry, 0, sizeof(log_entry));
    log_entry.xpdr_code[0] = 0x00;
    log_entry.xpdr_code[2] = 0xDE;
    log_entry.xpdr_code[3] = 0xAD;
    log_entry.xpdr_code[4] = 0xBE;
    log_entry.xpdr_code[5] = 0xEF;
    log_entry.date_time = 1717000000;
    log_entry.ms = 250;
    log_entry.loop_data = 3;

    uint8_t mac[6] = {0xAA,0xBB,0xCC,0x11,0x22,0x33};
    uint8_t out[64];
    gprs_record_kind_t kind;

    size_t n = gprs_build_record(&log_entry, 5, mac, 42, out, sizeof(out), &kind);
    assert(n == sizeof(gprs_rec_uhf_t));
    assert(kind == GPRS_RECORD_UHF);

    gprs_rec_uhf_t rec;
    memcpy(&rec, out, sizeof(rec));

    assert(rec.start_chr == 0x02);
    assert(rec.rec_len == 28);
    assert(rec.reader_no == 1);
    assert(rec.record_no == 42);
    assert(rec.chip_code == 0xDEADBEEF);
    assert(rec.seconds == 1717000000);
    assert(rec.ms == 250);
    assert(rec.mac_address[0] == 0x11 && rec.mac_address[1] == 0x22 && rec.mac_address[2] == 0x33);
    assert(rec.ultra_id == 5);
    assert(rec.antenna_no == 3);
    assert(rec.end_chr == 0x03);

    printf("test_build_uhf_record OK\n");
}

static void test_build_active_record(void) {
    nrf_record_t log_entry;
    memset(&log_entry, 0, sizeof(log_entry));
    memcpy(log_entry.xpdr_code, "ABC123", 6);
    log_entry.date_time = 1717000100;
    log_entry.ms = 500;
    log_entry.loop_data = (2 << 6);

    uint8_t mac[6] = {0,0,0,0x44,0x55,0x66};
    uint8_t out[64];
    gprs_record_kind_t kind;

    size_t n = gprs_build_record(&log_entry, 9, mac, 100, out, sizeof(out), &kind);
    assert(n == sizeof(gprs_rec_active_t));
    assert(kind == GPRS_RECORD_ACTIVE);

    gprs_rec_active_t rec;
    memcpy(&rec, out, sizeof(rec));

    assert(rec.rec_len == 30);
    assert(rec.record_no == 100);
    assert(memcmp(rec.chip_code, "ABC123", 6) == 0);
    assert(rec.ultra_id == 3);
    assert(rec.mac_address[0] == 0x44 && rec.mac_address[1] == 0x55 && rec.mac_address[2] == 0x66);
    assert(rec.antenna_no == 1);

    printf("test_build_active_record OK\n");
}

static void test_buffer_too_small(void) {
    nrf_record_t log_entry;
    memset(&log_entry, 0, sizeof(log_entry));
    uint8_t mac[6] = {0};
    uint8_t tiny[4];
    size_t n = gprs_build_record(&log_entry, 0, mac, 1, tiny, sizeof(tiny), NULL);
    assert(n == 0);
    printf("test_buffer_too_small OK\n");
}

int main(void) {
    test_struct_sizes_match_original_rec_len();
    test_build_uhf_record();
    test_build_active_record();
    test_buffer_too_small();
    printf("\nAll gprs_records tests passed.\n");
    return 0;
}
