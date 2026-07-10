#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "uhf_crc.h"

/* Hand-traced reference vectors: I manually stepped through the
 * nibble-CRC algorithm (init 0xFFFF, poly-0x1021 table, buf[0]
 * excluded) for these two single-byte-payload cases to validate the C
 * implementation independent of any ambiguous example data in the
 * original source comments. */
static void test_hand_traced_reference_vectors(void) {
    uint8_t buf1[2] = { 0xAA, 0x00 }; /* buf[0] value is irrelevant to the CRC */
    assert(uhf_calc_crc(buf1, 2) == 0xE1F0);

    uint8_t buf2[2] = { 0xAA, 0xFF };
    assert(uhf_calc_crc(buf2, 2) == 0xE10F);

    printf("test_hand_traced_reference_vectors OK\n");
}

static void test_header_byte_excluded(void) {
    /* buf[0] must have zero effect on the CRC -- only buf[1..len-1] matters */
    uint8_t buf_a[4] = { 0x00, 0x11, 0x22, 0x33 };
    uint8_t buf_b[4] = { 0xFF, 0x11, 0x22, 0x33 };
    assert(uhf_calc_crc(buf_a, 4) == uhf_calc_crc(buf_b, 4));
    printf("test_header_byte_excluded OK\n");
}

static void test_add_and_verify_roundtrip(void) {
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF;
    buf[1] = 5; /* data length field */
    buf[2] = 0xAA; buf[3] = 0x01; buf[4] = 0x02; buf[5] = 0x03; buf[6] = 0x04;

    /* uhf_verify_crc computes over len = frame_data_length + 5 and
     * expects the CRC written at [frame_data_length+5 .. +6], so
     * add_crc must be called with that same len. */
    uhf_add_crc(buf, (size_t)buf[1] + 5); /* writes CRC at buf[10], buf[11] */

    assert(uhf_verify_crc(buf, buf[1]) == 1);

    /* Corrupt one payload byte -- verification must fail */
    buf[3] ^= 0xFF;
    assert(uhf_verify_crc(buf, buf[1]) == 0);

    printf("test_add_and_verify_roundtrip OK\n");
}

static void test_verify_detects_corrupted_crc_bytes(void) {
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF;
    buf[1] = 3;
    buf[2] = 0x61; buf[3] = 0x00; buf[4] = 0x00;
    uhf_add_crc(buf, (size_t)buf[1] + 5);

    assert(uhf_verify_crc(buf, buf[1]) == 1);

    buf[buf[1] + 5] ^= 0x01; /* flip a bit in the CRC itself */
    assert(uhf_verify_crc(buf, buf[1]) == 0);

    printf("test_verify_detects_corrupted_crc_bytes OK\n");
}

int main(void) {
    test_hand_traced_reference_vectors();
    test_header_byte_excluded();
    test_add_and_verify_roundtrip();
    test_verify_detects_corrupted_crc_bytes();
    printf("\nAll uhf_crc tests passed.\n");
    return 0;
}
