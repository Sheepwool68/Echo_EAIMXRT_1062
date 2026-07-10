#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "finish_lynx_protocol.h"

static void test_time_string_format(void) {
    char buf[64];
    int n = finish_lynx_build_time_string(14, 5, 9, 123, buf, sizeof(buf));
    assert(n > 0);
    assert(n == (int)strlen(buf));
    assert(buf[0] == 0x01);
    assert(strcmp(buf + 1, "T,14:05:09.123\r\n") == 0);
    printf("test_time_string_format OK\n");
}

static void test_time_string_midnight_and_zero_ms(void) {
    char buf[64];
    int n = finish_lynx_build_time_string(0, 0, 0, 0, buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf + 1, "T,00:00:00.000\r\n") == 0);
    printf("test_time_string_midnight_and_zero_ms OK\n");
}

static void test_time_string_buffer_too_small(void) {
    char buf[5];
    int n = finish_lynx_build_time_string(1, 2, 3, 4, buf, sizeof(buf));
    assert(n == -1);
    printf("test_time_string_buffer_too_small OK\n");
}

static nrf_record_t make_record(uint32_t date_time, uint16_t ms, const char xpdr[6]) {
    nrf_record_t r;
    memset(&r, 0, sizeof(r));
    r.date_time = date_time;
    r.ms = ms;
    memcpy(r.xpdr_code, xpdr, 6);
    return r;
}

static void test_split_string_full_xpdr(void) {
    char buf[64];
    nrf_record_t rec = make_record(0, 999, "ABC123");
    int n = finish_lynx_build_split_string(&rec, 0, buf, sizeof(buf));
    assert(n > 0);
    assert(buf[0] == 0x01);
    /* epoch 0 = 1970-01-01 00:00:00 */
    assert(strcmp(buf + 1, "S,00:00:00.999,ABC123\r\n") == 0);
    printf("test_split_string_full_xpdr OK\n");
}

static void test_split_string_matches_known_epoch(void) {
    char buf[64];
    /* 2024-06-15 12:30:45 UTC = 1718454645, independently verified */
    nrf_record_t rec = make_record(1718454645u, 500, "XYZ\0\0\0");
    int n = finish_lynx_build_split_string(&rec, 1, buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf + 1, "S,12:30:45.500,XYZ\r\n") == 0); /* stops at embedded NUL after "XYZ" */
    printf("test_split_string_matches_known_epoch OK\n");
}

static void test_split_string_stops_at_embedded_nul(void) {
    char buf[64];
    nrf_record_t rec = make_record(0, 1, "AB\0DEF"); /* NUL at index 2 */
    int n = finish_lynx_build_split_string(&rec, 0, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, ",AB\r\n") != NULL); /* stops at the embedded NUL, not "ABDEF" */
    printf("test_split_string_stops_at_embedded_nul OK\n");
}

static void test_split_string_full_six_bytes_no_nul(void) {
    char buf[64];
    nrf_record_t rec = make_record(0, 1, "123456"); /* exactly 6 chars, no embedded NUL */
    int n = finish_lynx_build_split_string(&rec, 0, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, ",123456\r\n") != NULL);
    printf("test_split_string_full_six_bytes_no_nul OK\n");
}

static void test_split_string_is_rewind_param_ignored(void) {
    char buf_a[64], buf_b[64];
    nrf_record_t rec = make_record(12345, 42, "TAGONE");
    int n_a = finish_lynx_build_split_string(&rec, 0, buf_a, sizeof(buf_a));
    int n_b = finish_lynx_build_split_string(&rec, 1, buf_b, sizeof(buf_b));
    assert(n_a == n_b);
    assert(strcmp(buf_a, buf_b) == 0);
    printf("test_split_string_is_rewind_param_ignored OK\n");
}

static void test_split_string_buffer_too_small(void) {
    char buf[5];
    nrf_record_t rec = make_record(0, 1, "ABCDEF");
    int n = finish_lynx_build_split_string(&rec, 0, buf, sizeof(buf));
    assert(n == -1);
    printf("test_split_string_buffer_too_small OK\n");
}

int main(void) {
    test_time_string_format();
    test_time_string_midnight_and_zero_ms();
    test_time_string_buffer_too_small();
    test_split_string_full_xpdr();
    test_split_string_matches_known_epoch();
    test_split_string_stops_at_embedded_nul();
    test_split_string_full_six_bytes_no_nul();
    test_split_string_is_rewind_param_ignored();
    test_split_string_buffer_too_small();
    printf("\nAll finish_lynx_protocol tests passed.\n");
    return 0;
}
