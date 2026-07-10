/*
 * nand_log_flash_qspi.c
 *
 * See nand_log_flash_qspi.h -- confirmed for the Embedded Artists iMX
 * RT1062 OEM board's IS25WP128 (16MB QuadSPI NOR).
 *
 * REFACTORED: the actual flash program/erase/read primitives now live
 * in qspi_flash_raw.h/.c (shared with the new MCUboot secondary-slot
 * writer, fw_install_mcuboot.c) -- this file is now a thin littlefs
 * lfs_config adapter over those primitives, adding the partition
 * offset. Behavior is unchanged from the original single-file version.
 */

#include "nand_log_flash_qspi.h"
#include "qspi_flash_raw.h"
#include <string.h>

static uint32_t s_flash_offset;

static int flash_read_cb(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)c;
    uint32_t addr = s_flash_offset + block * QSPI_FLASH_SECTOR_SIZE + (uint32_t)off;
    return qspi_flash_read(addr, (uint8_t *)buffer, size);
}

static int flash_prog_cb(const struct lfs_config *c, lfs_block_t block,
                          lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)c;
    uint32_t addr = s_flash_offset + block * QSPI_FLASH_SECTOR_SIZE + (uint32_t)off;
    return qspi_flash_program(addr, (const uint8_t *)buffer, size);
}

static int flash_erase_cb(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    uint32_t addr = s_flash_offset + block * QSPI_FLASH_SECTOR_SIZE;
    return qspi_flash_erase_sector(addr);
}

static int flash_sync_cb(const struct lfs_config *c)
{
    (void)c;
    return 0; /* all writes above are already synchronous */
}

static uint8_t s_read_buffer[QSPI_FLASH_PAGE_SIZE];
static uint8_t s_prog_buffer[QSPI_FLASH_PAGE_SIZE];
static uint8_t s_lookahead_buffer[16];

int nand_log_flash_qspi_get_config(struct lfs_config *out_cfg,
                                    uint32_t flash_offset,
                                    uint32_t flash_size)
{
    if (flash_size % QSPI_FLASH_SECTOR_SIZE != 0) {
        return -1;
    }
    if ((uint64_t)flash_offset + flash_size > QSPI_FLASH_TOTAL_SIZE) {
        return -1;
    }

    s_flash_offset = flash_offset;

    memset(out_cfg, 0, sizeof(*out_cfg));
    out_cfg->read  = flash_read_cb;
    out_cfg->prog  = flash_prog_cb;
    out_cfg->erase = flash_erase_cb;
    out_cfg->sync  = flash_sync_cb;

    out_cfg->read_size = 1;
    out_cfg->prog_size = QSPI_FLASH_PAGE_SIZE;
    out_cfg->block_size = QSPI_FLASH_SECTOR_SIZE;
    out_cfg->block_count = flash_size / QSPI_FLASH_SECTOR_SIZE;
    out_cfg->cache_size = QSPI_FLASH_PAGE_SIZE;
    out_cfg->lookahead_size = sizeof(s_lookahead_buffer);
    out_cfg->block_cycles = 500; /* TODO: tune against flash endurance/write volume */

    out_cfg->read_buffer = s_read_buffer;
    out_cfg->prog_buffer = s_prog_buffer;
    out_cfg->lookahead_buffer = s_lookahead_buffer;

    return 0;
}
