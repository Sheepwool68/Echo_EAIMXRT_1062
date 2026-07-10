#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "crc32.h"

static void test_known_reference_vector(void) {
    /* The famous CRC-32 check value: CRC32("123456789") == 0xCBF43926 */
    const char *s = "123456789";
    uint32_t crc = crc32_compute((const uint8_t *)s, strlen(s));
    assert(crc == 0xCBF43926u);
    printf("test_known_reference_vector OK (0x%08X)\n", crc);
}

static void test_empty_input(void) {
    uint32_t crc = crc32_compute((const uint8_t *)"", 0);
    assert(crc == 0x00000000u);
    printf("test_empty_input OK\n");
}

static void test_incremental_matches_one_shot(void) {
    const char *s = "123456789";
    uint32_t one_shot = crc32_compute((const uint8_t *)s, strlen(s));

    uint32_t running = CRC32_INITIAL;
    running = crc32_update(running, (const uint8_t *)"123", 3);
    running = crc32_update(running, (const uint8_t *)"456", 3);
    running = crc32_update(running, (const uint8_t *)"789", 3);
    uint32_t incremental = crc32_finalize(running);

    assert(incremental == one_shot);
    printf("test_incremental_matches_one_shot OK\n");
}

static void test_single_bit_change_changes_crc(void) {
    uint8_t a[4] = {0x00, 0x00, 0x00, 0x00};
    uint8_t b[4] = {0x01, 0x00, 0x00, 0x00};
    assert(crc32_compute(a, 4) != crc32_compute(b, 4));
    printf("test_single_bit_change_changes_crc OK\n");
}

int main(void) {
    test_known_reference_vector();
    test_empty_input();
    test_incremental_matches_one_shot();
    test_single_bit_change_changes_crc();
    printf("\nAll crc32 tests passed.\n");
    return 0;
}
