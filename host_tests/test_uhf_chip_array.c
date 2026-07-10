#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "uhf_chip_array.h"

static void test_zero_code_ignored(void) {
    uhf_chip_entry_t chips[4];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx;

    uhf_chip_add_result_t r = uhf_chip_array_add(chips, 4, 0, -50, 1, 1000, 0, 1, &count, &unique, &idx);
    assert(r == UHF_CHIP_IGNORED_ZERO_CODE);
    assert(count == 0 && unique == 0);
    printf("test_zero_code_ignored OK\n");
}

static void test_add_new_chip(void) {
    uhf_chip_entry_t chips[4];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx = 99;

    uhf_chip_add_result_t r = uhf_chip_array_add(chips, 4, 0xAABBCCDD, -60, 2, 5000, 1234, 1, &count, &unique, &idx);
    assert(r == UHF_CHIP_ADDED_NEW);
    assert(idx == 0);
    assert(chips[0].chip_code == 0xAABBCCDD);
    assert(chips[0].rssi == -60);
    assert(chips[0].antenna == 2);
    assert(chips[0].seconds == 5000);
    assert(chips[0].ms == 234); /* 1234 % 1000 */
    assert(chips[0].reads == 1);
    assert(count == 1 && unique == 1);
    printf("test_add_new_chip OK\n");
}

static void test_rereads_increment_regardless_of_mode(void) {
    uhf_chip_entry_t chips[4];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx;

    uhf_chip_array_add(chips, 4, 0x11111111, -70, 1, 100, 0, 0, &count, &unique, &idx);
    uhf_chip_add_result_t r = uhf_chip_array_add(chips, 4, 0x11111111, -70, 1, 101, 0, 0, &count, &unique, &idx);
    assert(r == UHF_CHIP_REREAD);
    assert(chips[idx].reads == 2);
    r = uhf_chip_array_add(chips, 4, 0x11111111, -70, 1, 102, 0, 0, &count, &unique, &idx);
    assert(chips[idx].reads == 3);
    /* unique/total counts only bump on first sighting */
    assert(count == 1 && unique == 1);
    printf("test_rereads_increment_regardless_of_mode OK\n");
}

static void test_bug_preserved_mode_freezes_rssi(void) {
    /* fix_rssi_update_bug = 0: faithful to the original -- a stronger
     * RSSI on re-read should NOT update the stored fields. */
    uhf_chip_entry_t chips[4];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx;

    uhf_chip_array_add(chips, 4, 0x22222222, -80 /* weak first read */, 1, 100, 0, 0, &count, &unique, &idx);
    uhf_chip_array_add(chips, 4, 0x22222222, -30 /* much stronger */, 3, 105, 0, 0, &count, &unique, &idx);

    assert(chips[idx].rssi == -80);   /* frozen at first reading -- the bug */
    assert(chips[idx].antenna == 1);  /* also frozen */
    assert(chips[idx].seconds == 100); /* also frozen */
    assert(chips[idx].reads == 2);     /* reads count still correct */
    printf("test_bug_preserved_mode_freezes_rssi OK (bug faithfully reproduced)\n");
}

static void test_fixed_mode_updates_on_stronger_rssi(void) {
    /* fix_rssi_update_bug = 1: a stronger re-read SHOULD update fields. */
    uhf_chip_entry_t chips[4];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx;

    uhf_chip_array_add(chips, 4, 0x33333333, -80, 1, 100, 0, 1, &count, &unique, &idx);
    uhf_chip_array_add(chips, 4, 0x33333333, -30, 3, 105, 500, 1, &count, &unique, &idx);

    assert(chips[idx].rssi == -30);
    assert(chips[idx].antenna == 3);
    assert(chips[idx].seconds == 105);
    assert(chips[idx].ms == 500);
    assert(chips[idx].reads == 2);

    /* A subsequent WEAKER read should NOT overwrite the stronger one */
    uhf_chip_array_add(chips, 4, 0x33333333, -90, 2, 110, 0, 1, &count, &unique, &idx);
    assert(chips[idx].rssi == -30); /* still the stronger value */
    assert(chips[idx].reads == 3);

    printf("test_fixed_mode_updates_on_stronger_rssi OK\n");
}

static void test_array_full_drops_new_chip(void) {
    uhf_chip_entry_t chips[2];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx;

    uhf_chip_array_add(chips, 2, 0x1, -50, 1, 1, 0, 1, &count, &unique, &idx);
    uhf_chip_array_add(chips, 2, 0x2, -50, 1, 1, 0, 1, &count, &unique, &idx);
    /* array now full; a third DISTINCT chip should be dropped */
    uhf_chip_add_result_t r = uhf_chip_array_add(chips, 2, 0x3, -50, 1, 1, 0, 1, &count, &unique, &idx);
    assert(r == UHF_CHIP_ARRAY_FULL);
    assert(count == 2 && unique == 2); /* not incremented for the dropped read */
    printf("test_array_full_drops_new_chip OK\n");
}

static void test_entry_to_record_mapping(void) {
    uhf_chip_entry_t chip;
    memset(&chip, 0, sizeof(chip));
    chip.chip_code = 0x12345678;
    chip.rssi = -55;
    chip.antenna = 3;
    chip.seconds = 1717000000;
    chip.ms = 250;
    chip.reads = 4;
    chip.has_been_sent = 0;

    nrf_record_t rec;
    uhf_chip_entry_to_record(&chip, &rec);

    /* xpdr_code[0..1] = 0 (UHF marker), [2..5] = big-endian chip_code */
    assert((uint8_t)rec.xpdr_code[0] == 0x00);
    assert((uint8_t)rec.xpdr_code[1] == 0x00);
    assert((uint8_t)rec.xpdr_code[2] == 0x12);
    assert((uint8_t)rec.xpdr_code[3] == 0x34);
    assert((uint8_t)rec.xpdr_code[4] == 0x56);
    assert((uint8_t)rec.xpdr_code[5] == 0x78);

    assert(rec.max_RSSI == 55); /* negated to positive */
    assert(rec.wake_count == 4);
    assert(rec.battery == 0);
    assert(rec.date_time == 1717000000);
    assert(rec.ms == 250);
    assert(rec.loop_data == 3);
    assert(rec.log_id == 0); /* caller's job */

    printf("test_entry_to_record_mapping OK\n");
}

static void test_flush_aged_boundary(void) {
    uhf_chip_entry_t chips[4];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx;

    /* chip seen at t=100; aging window is 3s, so it should flush
     * exactly when now_seconds >= 103 (matches original's `<=`) */
    uhf_chip_array_add(chips, 4, 0xAAAA, -50, 1, 100, 0, 1, &count, &unique, &idx);

    nrf_record_t out[8];
    size_t n = uhf_chip_array_flush_aged(chips, 4, 102, out, 8, 1); /* not yet aged */
    assert(n == 0);
    assert(chips[idx].chip_code == 0xAAAA); /* still present */

    n = uhf_chip_array_flush_aged(chips, 4, 103, out, 8, 1); /* boundary: 100+3<=103 */
    assert(n == 1);
    assert(chips[idx].chip_code == 0); /* slot cleared */
    assert(out[0].log_id == 1);

    printf("test_flush_aged_boundary OK\n");
}

static void test_flush_aged_respects_max_out_cap(void) {
    uhf_chip_entry_t chips[5];
    memset(chips, 0, sizeof(chips));
    uint32_t count = 0, unique = 0;
    size_t idx;

    for (uint32_t c = 1; c <= 5; c++) {
        uhf_chip_array_add(chips, 5, c, -50, 1, 100, 0, 1, &count, &unique, &idx);
    }

    nrf_record_t out[3];
    size_t n = uhf_chip_array_flush_aged(chips, 5, 200, out, 3, 10);
    assert(n == 3); /* capped at max_out even though 5 are eligible */

    /* remaining 2 still present, will flush on a later call */
    size_t remaining = 0;
    for (size_t i = 0; i < 5; i++) {
        if (chips[i].chip_code != 0) remaining++;
    }
    assert(remaining == 2);

    printf("test_flush_aged_respects_max_out_cap OK\n");
}

int main(void) {
    test_zero_code_ignored();
    test_add_new_chip();
    test_rereads_increment_regardless_of_mode();
    test_bug_preserved_mode_freezes_rssi();
    test_fixed_mode_updates_on_stronger_rssi();
    test_array_full_drops_new_chip();
    test_entry_to_record_mapping();
    test_flush_aged_boundary();
    test_flush_aged_respects_max_out_cap();
    printf("\nAll uhf_chip_array tests passed.\n");
    return 0;
}
