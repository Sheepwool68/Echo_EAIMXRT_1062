/*
 * lfs_mflash.c
 *
 * Adapted from the Embedded Artists SDK's littlefs_shell example --
 * see lfs_mflash.h for what changed (LittleFS_config defined directly
 * here instead of via a Config-Tools "mflash" component this project
 * doesn't have). The block-device callbacks themselves
 * (read/prog/erase/sync) are unchanged from the reference.
 *
 * Static buffers (not lfs_malloc/heap) for read/prog/lookahead --
 * deliberate: this project's heap is only 4KB (see the linker script's
 * _HeapSize), and static allocation avoids any heap-pressure surprise
 * for a subsystem this new.
 */

#include "lfs_mflash.h"
#include "fsl_debug_console.h"

struct lfs_mflash_ctx LittleFS_ctx = {LITTLEFS_START_ADDR};

#define LFS_READ_SIZE      MFLASH_PAGE_SIZE
#define LFS_PROG_SIZE      MFLASH_PAGE_SIZE
#define LFS_CACHE_SIZE     MFLASH_PAGE_SIZE
#define LFS_LOOKAHEAD_SIZE 16u

static uint8_t s_lfs_read_buffer[LFS_READ_SIZE];
static uint8_t s_lfs_prog_buffer[LFS_PROG_SIZE];
static uint32_t s_lfs_lookahead_buffer[LFS_LOOKAHEAD_SIZE / sizeof(uint32_t)];

int lfs_mflash_read(const struct lfs_config *lfsc, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
    struct lfs_mflash_ctx *ctx;
    uint32_t flash_addr;

    assert(lfsc);
    ctx = (struct lfs_mflash_ctx *)lfsc->context;
    assert(ctx);

    flash_addr = ctx->start_addr + block * lfsc->block_size + off;

    if (mflash_drv_read(flash_addr, buffer, size) != kStatus_Success)
        return LFS_ERR_IO;

    return LFS_ERR_OK;
}

int lfs_mflash_prog(
    const struct lfs_config *lfsc, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{
    status_t status = kStatus_Success;
    struct lfs_mflash_ctx *ctx;
    uint32_t flash_addr;

    assert(lfsc);
    ctx = (struct lfs_mflash_ctx *)lfsc->context;
    assert(ctx);

    flash_addr = ctx->start_addr + block * lfsc->block_size + off;

    assert(mflash_drv_is_page_aligned(size));

    for (uint32_t page_ofs = 0; page_ofs < size; page_ofs += MFLASH_PAGE_SIZE)
    {
        status = mflash_drv_page_program(flash_addr + page_ofs, (void *)((uintptr_t)buffer + page_ofs));
        if (status != kStatus_Success)
            break;
    }

    if (status != kStatus_Success)
        return LFS_ERR_IO;

    return LFS_ERR_OK;
}

int lfs_mflash_erase(const struct lfs_config *lfsc, lfs_block_t block)
{
    status_t status = kStatus_Success;
    struct lfs_mflash_ctx *ctx;
    uint32_t flash_addr;

    assert(lfsc);
    ctx = (struct lfs_mflash_ctx *)lfsc->context;
    assert(ctx);

    flash_addr = ctx->start_addr + block * lfsc->block_size;

    for (uint32_t sector_ofs = 0; sector_ofs < lfsc->block_size; sector_ofs += MFLASH_SECTOR_SIZE)
    {
        status = mflash_drv_sector_erase(flash_addr + sector_ofs);
        if (status != kStatus_Success)
            break;
    }

    if (status != kStatus_Success)
        return LFS_ERR_IO;

    return LFS_ERR_OK;
}

int lfs_mflash_sync(const struct lfs_config *lfsc)
{
    (void)lfsc;
    return LFS_ERR_OK;
}

const struct lfs_config LittleFS_config = {
    .context      = &LittleFS_ctx,
    .read         = lfs_mflash_read,
    .prog         = lfs_mflash_prog,
    .erase        = lfs_mflash_erase,
    .sync         = lfs_mflash_sync,
    .read_size    = LFS_READ_SIZE,
    .prog_size    = LFS_PROG_SIZE,
    .block_size   = MFLASH_SECTOR_SIZE,
    .block_count  = LITTLEFS_BLOCK_COUNT,
    .block_cycles = 500,
    .cache_size   = LFS_CACHE_SIZE,
    .lookahead_size   = LFS_LOOKAHEAD_SIZE,
    .read_buffer      = s_lfs_read_buffer,
    .prog_buffer      = s_lfs_prog_buffer,
    .lookahead_buffer = s_lfs_lookahead_buffer,
};

int lfs_get_default_config(struct lfs_config *lfsc)
{
    *lfsc = LittleFS_config;
    return 0;
}

int lfs_storage_init(const struct lfs_config *lfsc)
{
    (void)lfsc;
    return (int)mflash_drv_init();
}
