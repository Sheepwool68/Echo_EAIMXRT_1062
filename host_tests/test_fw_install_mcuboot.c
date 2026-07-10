#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "fw_install_mcuboot.h"
#include "qspi_flash_raw.h"

#define MOCK_SLOT_SIZE (QSPI_FLASH_SECTOR_SIZE * 8)
static uint8_t g_mock_flash[MOCK_SLOT_SIZE];
static int g_erase_calls;
static uint32_t g_erased_addrs[64];
static int g_program_calls;
static int g_force_erase_fail;
static int g_force_program_fail;

static void mock_flash_reset(void) {
    memset(g_mock_flash, 0xAA, sizeof(g_mock_flash));
    g_erase_calls = 0;
    g_program_calls = 0;
    g_force_erase_fail = 0;
    g_force_program_fail = 0;
}

static int mock_erase(uint32_t addr) {
    if (g_force_erase_fail) return -1;
    if (g_erase_calls < 64) g_erased_addrs[g_erase_calls] = addr;
    g_erase_calls++;
    memset(&g_mock_flash[addr], 0xFF, QSPI_FLASH_SECTOR_SIZE);
    return 0;
}

static int mock_program(uint32_t addr, const uint8_t *data, size_t len) {
    if (g_force_program_fail) return -1;
    g_program_calls++;
    memcpy(&g_mock_flash[addr], data, len);
    return 0;
}

static void test_single_small_write(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, MOCK_SLOT_SIZE, mock_erase, mock_program);

    const uint8_t chunk[] = "HELLO_FIRMWARE";
    int rc = fw_mcuboot_install_sink(&ctx, chunk, sizeof(chunk));

    assert(rc == 0);
    assert(g_erase_calls == 1);
    assert(memcmp(g_mock_flash, chunk, sizeof(chunk)) == 0);
    assert(ctx.write_cursor == sizeof(chunk));

    printf("test_single_small_write OK\n");
}

static void test_multiple_chunks_same_sector_no_re_erase(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, MOCK_SLOT_SIZE, mock_erase, mock_program);

    fw_mcuboot_install_sink(&ctx, (const uint8_t *)"AAAA", 4);
    fw_mcuboot_install_sink(&ctx, (const uint8_t *)"BBBB", 4);
    fw_mcuboot_install_sink(&ctx, (const uint8_t *)"CCCC", 4);

    assert(g_erase_calls == 1);
    assert(g_program_calls == 3);
    assert(memcmp(g_mock_flash, "AAAABBBBCCCC", 12) == 0);

    printf("test_multiple_chunks_same_sector_no_re_erase OK\n");
}

static void test_write_spanning_multiple_sectors(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, MOCK_SLOT_SIZE, mock_erase, mock_program);

    size_t chunk_len = QSPI_FLASH_SECTOR_SIZE + (QSPI_FLASH_SECTOR_SIZE / 2);
    uint8_t *chunk = malloc(chunk_len);
    memset(chunk, 0x42, chunk_len);

    int rc = fw_mcuboot_install_sink(&ctx, chunk, chunk_len);

    assert(rc == 0);
    assert(g_erase_calls == 2);
    assert(g_erased_addrs[0] == 0);
    assert(g_erased_addrs[1] == QSPI_FLASH_SECTOR_SIZE);
    assert(ctx.write_cursor == chunk_len);

    free(chunk);
    printf("test_write_spanning_multiple_sectors OK\n");
}

static void test_second_write_into_new_sector_erases_only_that_one(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, MOCK_SLOT_SIZE, mock_erase, mock_program);

    uint8_t *sector0 = malloc(QSPI_FLASH_SECTOR_SIZE);
    memset(sector0, 0x11, QSPI_FLASH_SECTOR_SIZE);
    fw_mcuboot_install_sink(&ctx, sector0, QSPI_FLASH_SECTOR_SIZE);
    assert(g_erase_calls == 1);

    fw_mcuboot_install_sink(&ctx, (const uint8_t *)"XYZ", 3);
    assert(g_erase_calls == 2);
    assert(g_erased_addrs[1] == QSPI_FLASH_SECTOR_SIZE);

    free(sector0);
    printf("test_second_write_into_new_sector_erases_only_that_one OK\n");
}

static void test_overflow_rejected(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, 16, mock_erase, mock_program);

    uint8_t chunk[32];
    memset(chunk, 0x99, sizeof(chunk));
    int rc = fw_mcuboot_install_sink(&ctx, chunk, sizeof(chunk));

    assert(rc != 0);
    assert(g_program_calls == 0);

    printf("test_overflow_rejected OK\n");
}

static void test_erase_failure_propagates(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, MOCK_SLOT_SIZE, mock_erase, mock_program);
    g_force_erase_fail = 1;

    int rc = fw_mcuboot_install_sink(&ctx, (const uint8_t *)"data", 4);
    assert(rc != 0);
    printf("test_erase_failure_propagates OK\n");
}

static void test_program_failure_propagates(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, MOCK_SLOT_SIZE, mock_erase, mock_program);
    g_force_program_fail = 1;

    int rc = fw_mcuboot_install_sink(&ctx, (const uint8_t *)"data", 4);
    assert(rc != 0);
    printf("test_program_failure_propagates OK\n");
}

static void test_nonzero_slot_offset(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    uint32_t fake_offset = QSPI_FLASH_SECTOR_SIZE * 3;
    fw_mcuboot_install_init(&ctx, fake_offset, MOCK_SLOT_SIZE - fake_offset, mock_erase, mock_program);

    fw_mcuboot_install_sink(&ctx, (const uint8_t *)"OFFSET_TEST", 11);

    assert(g_erased_addrs[0] == fake_offset);
    assert(memcmp(&g_mock_flash[fake_offset], "OFFSET_TEST", 11) == 0);

    printf("test_nonzero_slot_offset OK\n");
}

static int g_boot_set_pending_calls;
static int g_last_image_index;
static uint8_t g_last_permanent;

int boot_set_pending_multi(int image_index, uint8_t permanent) {
    g_boot_set_pending_calls++;
    g_last_image_index = image_index;
    g_last_permanent = permanent;
    return 0;
}

static void test_finalize_calls_boot_set_pending(void) {
    fw_mcuboot_install_ctx_t ctx;
    mock_flash_reset();
    fw_mcuboot_install_init(&ctx, 0, MOCK_SLOT_SIZE, mock_erase, mock_program);
    g_boot_set_pending_calls = 0;

    int rc = fw_mcuboot_install_finalize(&ctx);

    assert(rc == 0);
    assert(g_boot_set_pending_calls == 1);
    assert(g_last_image_index == 0);
    assert(g_last_permanent == 0);

    printf("test_finalize_calls_boot_set_pending OK\n");
}

int main(void) {
    test_single_small_write();
    test_multiple_chunks_same_sector_no_re_erase();
    test_write_spanning_multiple_sectors();
    test_second_write_into_new_sector_erases_only_that_one();
    test_overflow_rejected();
    test_erase_failure_propagates();
    test_program_failure_propagates();
    test_nonzero_slot_offset();
    test_finalize_calls_boot_set_pending();
    printf("\nAll fw_install_mcuboot tests passed.\n");
    return 0;
}
