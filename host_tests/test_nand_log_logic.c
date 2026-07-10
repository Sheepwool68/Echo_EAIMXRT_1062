#include <stdio.h>
#include <assert.h>
#include "nand_log_logic.h"

static void test_percent_full_basic(void) {
    assert(nand_log_percent_full(0, 1000000) == 0);
    assert(nand_log_percent_full(500000, 1000000) == 50);
    assert(nand_log_percent_full(1000000, 1000000) == 100);
    assert(nand_log_percent_full(1200000, 1000000) == 100); /* over max, clamp */
    printf("test_percent_full_basic OK\n");
}

static void test_percent_full_zero_max(void) {
    /* Unconfigured/zero max shouldn't crash or divide by zero */
    assert(nand_log_percent_full(500, 0) == 0);
    printf("test_percent_full_zero_max OK\n");
}

static void test_auto_reset_threshold(void) {
    assert(nand_log_should_auto_reset(96) == 0);
    assert(nand_log_should_auto_reset(97) == 1); /* boundary: >= 97, matches original's `>=97` */
    assert(nand_log_should_auto_reset(98) == 1);
    assert(nand_log_should_auto_reset(100) == 1);
    assert(nand_log_should_auto_reset(0) == 0);
    printf("test_auto_reset_threshold OK\n");
}

static void test_record_count_from_size(void) {
    uint64_t rec_size = sizeof(nrf_record_t);
    assert(nand_log_record_count_from_size(0) == 0);
    assert(nand_log_record_count_from_size(rec_size) == 1);
    assert(nand_log_record_count_from_size(rec_size * 100) == 100);
    /* partial trailing record (e.g. a torn write) truncates down, not up */
    assert(nand_log_record_count_from_size(rec_size * 3 + 1) == 3);
    printf("test_record_count_from_size OK (record size = %llu bytes)\n",
           (unsigned long long)rec_size);
}

static void test_offset_for_record(void) {
    uint64_t rec_size = sizeof(nrf_record_t);
    assert(nand_log_offset_for_record(0) == 0);
    assert(nand_log_offset_for_record(1) == rec_size);
    assert(nand_log_offset_for_record(10) == rec_size * 10);
    printf("test_offset_for_record OK\n");
}

int main(void) {
    test_percent_full_basic();
    test_percent_full_zero_max();
    test_auto_reset_threshold();
    test_record_count_from_size();
    test_offset_for_record();
    printf("\nAll nand_log_logic tests passed.\n");
    return 0;
}
