#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "neo_m8t_protocol.h"

static void test_checksum_known_vector(void) {
    uint8_t buf[7] = {0xB5, 0x62, 0x01, 0x02, 0x03, 0, 0};
    ubx_compute_checksum(buf, 7);
    assert(buf[5] == 6);
    assert(buf[6] == 10);
    printf("test_checksum_known_vector OK\n");
}

static void test_checksum_matches_real_config_message(void) {
    uint8_t buf[40] = {0xB5,0x62,0x06,0x31,0x20,0x00,
        0x00,0x01,0x00,0x00,0x32,0x00,0x00,0x00,
        0x40,0x42,0x0F,0x00,0x40,0x42,0x0F,0x00,
        0x00,0x00,0x00,0x00,0xA0,0x86,0x01,0x00,
        0x00,0x00,0x00,0x01,0xF7,0x00,0x00,0x00,
        0x00,0x00};
    uint8_t before[40];
    memcpy(before, buf, 40);
    ubx_compute_checksum(buf, 40);
    assert(memcmp(before, buf, 38) == 0);
    printf("test_checksum_matches_real_config_message OK\n");
}

static void test_find_sync_at_start(void) {
    uint8_t buf[] = {0xB5, 0x62, 0x01, 0x02};
    assert(ubx_find_sync(buf, sizeof(buf)) == 0);
    printf("test_find_sync_at_start OK\n");
}

static void test_find_sync_offset(void) {
    uint8_t buf[] = {0xFF, 0xFF, 0xB5, 0x62, 0x01};
    assert(ubx_find_sync(buf, sizeof(buf)) == 2);
    printf("test_find_sync_offset OK\n");
}

static void test_find_sync_not_found(void) {
    uint8_t buf[] = {0xFF, 0xFF, 0xFF, 0xFF};
    assert(ubx_find_sync(buf, sizeof(buf)) == -1);
    printf("test_find_sync_not_found OK\n");
}

static void test_find_sync_too_short(void) {
    uint8_t buf[] = {0xB5};
    assert(ubx_find_sync(buf, sizeof(buf)) == -1);
    printf("test_find_sync_too_short OK\n");
}

static void test_classify_ack(void) {
    uint8_t ack[] = {0xB5, 0x62, 0x05, 0x01};
    uint8_t nak[] = {0xB5, 0x62, 0x05, 0x00};
    uint8_t neither[] = {0xB5, 0x62, 0x05, 0x42};
    assert(ubx_classify_ack(ack) == 1);
    assert(ubx_classify_ack(nak) == 0);
    assert(ubx_classify_ack(neither) == -1);
    printf("test_classify_ack OK\n");
}

static void test_is_nav_pvt(void) {
    uint8_t good[] = {0xB5, 0x62, 0x01, 0x07, 0x5C};
    uint8_t wrong_class[] = {0xB5, 0x62, 0x02, 0x07, 0x5C};
    uint8_t wrong_id[] = {0xB5, 0x62, 0x01, 0x08, 0x5C};
    uint8_t wrong_len[] = {0xB5, 0x62, 0x01, 0x07, 0x00};
    assert(ubx_is_nav_pvt(good) == 1);
    assert(ubx_is_nav_pvt(wrong_class) == 0);
    assert(ubx_is_nav_pvt(wrong_id) == 0);
    assert(ubx_is_nav_pvt(wrong_len) == 0);
    printf("test_is_nav_pvt OK\n");
}

static uint8_t *make_pvt_buffer(uint8_t buf[38], uint16_t year, uint8_t month, uint8_t day,
                                 uint8_t hours, uint8_t minutes, uint8_t seconds,
                                 uint8_t valid_flags, uint8_t fix_type, uint8_t flags,
                                 uint8_t sats, int32_t lon, int32_t lat) {
    memset(buf, 0, 38);
    buf[0] = UBX_SYNC1; buf[1] = UBX_SYNC2;
    buf[2] = 0x01; buf[3] = 0x07; buf[4] = 0x5C; buf[5] = 0x00;
    buf[10] = (uint8_t)(year & 0xFF);
    buf[11] = (uint8_t)((year >> 8) & 0xFF);
    buf[12] = month;
    buf[13] = day;
    buf[14] = hours;
    buf[15] = minutes;
    buf[16] = seconds;
    buf[17] = valid_flags;
    buf[26] = fix_type;
    buf[27] = flags;
    buf[29] = sats;
    buf[30] = (uint8_t)(lon & 0xFF);
    buf[31] = (uint8_t)((lon >> 8) & 0xFF);
    buf[32] = (uint8_t)((lon >> 16) & 0xFF);
    buf[33] = (uint8_t)((lon >> 24) & 0xFF);
    buf[34] = (uint8_t)(lat & 0xFF);
    buf[35] = (uint8_t)((lat >> 8) & 0xFF);
    buf[36] = (uint8_t)((lat >> 16) & 0xFF);
    buf[37] = (uint8_t)((lat >> 24) & 0xFF);
    return buf;
}

static void test_parse_nav_pvt_full_fields(void) {
    uint8_t buf[38];
    neo_pvt_t pvt;
    make_pvt_buffer(buf, 2024, 6, 15, 12, 30, 45, 0x07, 3, 0x01, 9, 185432100, -320562786);

    ubx_parse_nav_pvt(buf, &pvt);

    assert(pvt.year == 2024);
    assert(pvt.month == 6);
    assert(pvt.day == 15);
    assert(pvt.hours == 12);
    assert(pvt.minutes == 30);
    assert(pvt.seconds == 45);
    assert(pvt.sats == 9);
    assert(pvt.lon == 185432100);
    assert(pvt.lat == -320562786);
    assert(pvt.status == 1);

    printf("test_parse_nav_pvt_full_fields OK\n");
}

static void test_parse_nav_pvt_preserves_precedence_quirk(void) {
    uint8_t buf[38];
    neo_pvt_t pvt;

    make_pvt_buffer(buf, 2024, 1, 1, 0, 0, 0, 0x01, 3, 0x01, 5, 0, 0);
    ubx_parse_nav_pvt(buf, &pvt);
    assert(pvt.status == 1);

    make_pvt_buffer(buf, 2024, 1, 1, 0, 0, 0, 0x00, 3, 0x01, 5, 0, 0);
    ubx_parse_nav_pvt(buf, &pvt);
    assert(pvt.status == 0);

    make_pvt_buffer(buf, 2024, 1, 1, 0, 0, 0, 0x07, 0, 0x01, 5, 0, 0);
    ubx_parse_nav_pvt(buf, &pvt);
    assert(pvt.status == 0);

    printf("test_parse_nav_pvt_preserves_precedence_quirk OK\n");
}

static void test_parse_nav_pvt_negative_coordinates(void) {
    uint8_t buf[38];
    neo_pvt_t pvt;
    make_pvt_buffer(buf, 2024, 1, 1, 0, 0, 0, 0x07, 3, 0x01, 5, -1000000000, -500000000);
    ubx_parse_nav_pvt(buf, &pvt);
    assert(pvt.lon == -1000000000);
    assert(pvt.lat == -500000000);
    printf("test_parse_nav_pvt_negative_coordinates OK\n");
}

static void test_nmea_format_known_values(void) {
    char buf[64];
    int n = neo_format_nmea(-320562786, 185432100, buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf, "3203.37671,S,01832.59260,E") == 0);
    printf("test_nmea_format_known_values OK\n");
}

static void test_nmea_format_buffer_too_small(void) {
    char buf[10];
    int n = neo_format_nmea(-320562786, 185432100, buf, sizeof(buf));
    assert(n == -1);
    printf("test_nmea_format_buffer_too_small OK\n");
}

static void test_dd_dms_format_known_values(void) {
    char buf[64];
    int n = neo_format_dd_dms(-320562786, 185432100, buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf, "32.0562786,S,18.5432100,E") == 0);
    printf("test_dd_dms_format_known_values OK\n");
}

static void test_nmea_format_positive_coordinates(void) {
    char buf[64];
    int n = neo_format_nmea(320562786, 185432100, buf, sizeof(buf));
    assert(n > 0);
    assert(strstr(buf, ",N,") != NULL);
    assert(strstr(buf, ",E") != NULL);
    printf("test_nmea_format_positive_coordinates OK\n");
}

int main(void) {
    test_checksum_known_vector();
    test_checksum_matches_real_config_message();
    test_find_sync_at_start();
    test_find_sync_offset();
    test_find_sync_not_found();
    test_find_sync_too_short();
    test_classify_ack();
    test_is_nav_pvt();
    test_parse_nav_pvt_full_fields();
    test_parse_nav_pvt_preserves_precedence_quirk();
    test_parse_nav_pvt_negative_coordinates();
    test_nmea_format_known_values();
    test_nmea_format_buffer_too_small();
    test_dd_dms_format_known_values();
    test_nmea_format_positive_coordinates();
    printf("\nAll neo_m8t_protocol tests passed.\n");
    return 0;
}
