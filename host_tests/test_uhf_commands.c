#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "uhf_commands.h"
#include "uhf_crc.h"

static void test_get_version(void) {
    uint8_t buf[16];
    size_t n = uhf_build_get_version(buf, sizeof(buf));
    assert(n == 5);
    assert(buf[0] == 0xff && buf[1] == 0x00 && buf[2] == 0x03);
    assert(uhf_calc_crc(buf, 3) == (uint16_t)((buf[3] << 8) | buf[4]));
    printf("test_get_version OK\n");
}

static void test_dc_check_literal(void) {
    uint8_t buf[16];
    size_t n = uhf_build_dc_check(buf, sizeof(buf));
    assert(n == 6);
    uint8_t expected[6] = {0xff, 0x01, 0x61, 0x05, 0xbd, 0xb8};
    assert(memcmp(buf, expected, 6) == 0);
    printf("test_dc_check_literal OK\n");
}

static void test_return_loss_test_eu_vs_fcc(void) {
    uint8_t buf[32];
    size_t n = uhf_build_return_loss_test(buf, sizeof(buf), UHF_REGION_EU, 2);
    assert(n == 27);
    assert(buf[17] == 2);
    assert(buf[23] == (uint8_t)(0x6F + 2));
    assert(uhf_calc_crc(buf, 25) == (uint16_t)((buf[25] << 8) | buf[26]));

    size_t n2 = uhf_build_return_loss_test(buf, sizeof(buf), UHF_REGION_FCC, 3);
    assert(n2 == 27);
    assert(buf[17] == 3);
    assert(buf[23] == (uint8_t)(0x03 + 3));
    assert(uhf_calc_crc(buf, 25) == (uint16_t)((buf[25] << 8) | buf[26]));

    printf("test_return_loss_test_eu_vs_fcc OK\n");
}

static void test_antenna_enable_all_four(void) {
    uint8_t buf[32];
    uint32_t duty = 0;
    size_t n = uhf_build_antenna_enable(buf, sizeof(buf), 0x0F, &duty);

    assert(n == 14);
    assert(buf[1] == 9);
    assert(duty == 4 * 40 + 10);

    assert(buf[4] == 1 && buf[5] == 1);
    assert(buf[6] == 2 && buf[7] == 2);
    assert(buf[8] == 3 && buf[9] == 3);
    assert(buf[10] == 4 && buf[11] == 4);

    assert(uhf_calc_crc(buf, 12) == (uint16_t)((buf[12] << 8) | buf[13]));

    printf("test_antenna_enable_all_four OK (duty=%u)\n", duty);
}

static void test_antenna_enable_single_antenna(void) {
    uint8_t buf[32];
    uint32_t duty = 0;
    size_t n = uhf_build_antenna_enable(buf, sizeof(buf), 0x04, &duty);

    assert(n == 8);
    assert(buf[1] == 3);
    assert(buf[4] == 2 && buf[5] == 2);
    assert(duty == 1 * 40 + 10);

    printf("test_antenna_enable_single_antenna OK\n");
}

static void test_antenna_enable_no_antennas(void) {
    uint8_t buf[32];
    uint32_t duty = 999;
    size_t n = uhf_build_antenna_enable(buf, sizeof(buf), 0x00, &duty);
    assert(n == 0);
    assert(duty == 0);
    printf("test_antenna_enable_no_antennas OK\n");
}

static void test_power_set(void) {
    uint8_t buf[32];
    size_t n = uhf_build_power_set(buf, sizeof(buf), UHF_POWER_31_5DBM);
    assert(n == 26);
    assert(buf[0] == 0xff && buf[1] == 0x15 && buf[2] == 0x91 && buf[3] == 0x03);
    assert(buf[4] == 0x01 && buf[5] == 0x0C && buf[6] == 0x4E);
    assert(buf[19] == 0x04 && buf[20] == 0x0C && buf[21] == 0x4E);
    assert(uhf_calc_crc(buf, 24) == (uint16_t)((buf[24] << 8) | buf[25]));
    printf("test_power_set OK\n");
}

static void test_region_frequency_fcc_matches_original_literal(void) {
    uint8_t buf[64];
    size_t n = uhf_build_region_frequency(buf, sizeof(buf), UHF_REGION_FCC, 0);
    assert(n == 6);
    uint8_t expected[6] = {0xff, 0x01, 0x97, 0x01, 0x4b, 0xbc};
    assert(memcmp(buf, expected, 6) == 0);
    printf("test_region_frequency_fcc_matches_original_literal OK\n");
}

static void test_region_frequency_china_matches_original_literal(void) {
    uint8_t buf[64];
    size_t n = uhf_build_region_frequency(buf, sizeof(buf), UHF_REGION_CHINA, 0);
    assert(n == 6);
    uint8_t expected[6] = {0xff, 0x01, 0x97, 0x06, 0x4b, 0xbb};
    assert(memcmp(buf, expected, 6) == 0);
    printf("test_region_frequency_china_matches_original_literal OK\n");
}

static void test_region_frequency_au(void) {
    uint8_t buf[64];
    size_t n = uhf_build_region_frequency(buf, sizeof(buf), UHF_REGION_AU, 0);
    assert(n == 45);
    assert(buf[0] == 0xff && buf[1] == 0x28 && buf[2] == 0x95);
    assert(buf[3] == 0x00 && buf[4] == 0x0E && buf[5] == 0x0A && buf[6] == 0xBA);
    assert(uhf_calc_crc(buf, 43) == (uint16_t)((buf[43] << 8) | buf[44]));
    printf("test_region_frequency_au OK\n");
}

static void test_region_frequency_eu_channel_split(void) {
    uint8_t buf[64];
    size_t n = uhf_build_region_frequency(buf, sizeof(buf), UHF_REGION_EU, 3);
    assert(n == 6);
    assert(buf[3] == 0x08);

    n = uhf_build_region_frequency(buf, sizeof(buf), UHF_REGION_EU, 8);
    assert(n == 13);
    assert(buf[5] == 0x35 && buf[6] == 0xA4);

    n = uhf_build_region_frequency(buf, sizeof(buf), UHF_REGION_EU, 9);
    assert(n == 13);
    assert(buf[5] == 0x3A && buf[6] == 0x54);

    printf("test_region_frequency_eu_channel_split OK\n");
}

static void test_mode_sequence_finish_line(void) {
    uint8_t buf[64];
    size_t lens[3];
    int count = 0;
    size_t total = uhf_build_mode_sequence(buf, sizeof(buf), 1, lens, &count);

    assert(count == 3);
    assert(lens[0] == 9 && lens[1] == 8 && lens[2] == 8);
    assert(total == 25);

    assert(buf[9 + 5] == 0x00);
    assert(buf[17 + 5] == 0x6B);

    printf("test_mode_sequence_finish_line OK\n");
}

static void test_mode_sequence_start_line(void) {
    uint8_t buf[64];
    size_t lens[3];
    int count = 0;
    uhf_build_mode_sequence(buf, sizeof(buf), 0, lens, &count);

    assert(buf[9 + 5] == 0x01);
    assert(buf[17 + 5] == 0x71);

    printf("test_mode_sequence_start_line OK\n");
}

static void test_start_reading_subcrc_and_outer_crc(void) {
    uint8_t buf[32];
    size_t n = uhf_build_start_reading(buf, sizeof(buf), 0);

    assert(n == 24);
    assert(buf[18] == 0x20);

    uint8_t expected_sub = uhf_sub_checksum(&buf[13], 7);
    assert(buf[20] == expected_sub);

    assert(uhf_calc_crc(buf, 22) == (uint16_t)((buf[22] << 8) | buf[23]));

    uint8_t buf2[32];
    uhf_build_start_reading(buf2, sizeof(buf2), 1);
    assert(buf2[18] == 0x80);
    assert(buf2[20] != buf[20]);
    assert(memcmp(&buf[22], &buf2[22], 2) != 0);

    printf("test_start_reading_subcrc_and_outer_crc OK\n");
}

static void test_stop_reading_literal_and_crc(void) {
    uint8_t buf[32];
    size_t n = uhf_build_stop_reading(buf, sizeof(buf));
    assert(n == 19);
    uint8_t expected[19] = {
        0xFF,0x0E,0xAA,'M','o','d','u','l','e','t','e','c','h',0xAA,0x49,
        0xF3,0xBB,0x03,0x91
    };
    assert(memcmp(buf, expected, 19) == 0);
    assert(uhf_calc_crc(buf, 17) == (uint16_t)((buf[17] << 8) | buf[18]));
    printf("test_stop_reading_literal_and_crc OK\n");
}

static void test_get_temperature_literal(void) {
    uint8_t buf[16];
    size_t n = uhf_build_get_temperature(buf, sizeof(buf));
    assert(n == 5);
    uint8_t expected[5] = {0xFF, 0x00, 0x72, 0x1D, 0x7D};
    assert(memcmp(buf, expected, 5) == 0);
    printf("test_get_temperature_literal OK\n");
}

static void test_buffer_too_small_returns_zero(void) {
    uint8_t tiny[2];
    assert(uhf_build_get_version(tiny, sizeof(tiny)) == 0);
    assert(uhf_build_dc_check(tiny, sizeof(tiny)) == 0);
    assert(uhf_build_power_set(tiny, sizeof(tiny), UHF_POWER_31_5DBM) == 0);
    printf("test_buffer_too_small_returns_zero OK\n");
}

int main(void) {
    test_get_version();
    test_dc_check_literal();
    test_return_loss_test_eu_vs_fcc();
    test_antenna_enable_all_four();
    test_antenna_enable_single_antenna();
    test_antenna_enable_no_antennas();
    test_power_set();
    test_region_frequency_fcc_matches_original_literal();
    test_region_frequency_china_matches_original_literal();
    test_region_frequency_au();
    test_region_frequency_eu_channel_split();
    test_mode_sequence_finish_line();
    test_mode_sequence_start_line();
    test_start_reading_subcrc_and_outer_crc();
    test_stop_reading_literal_and_crc();
    test_get_temperature_literal();
    test_buffer_too_small_returns_zero();
    printf("\nAll uhf_commands tests passed.\n");
    return 0;
}
