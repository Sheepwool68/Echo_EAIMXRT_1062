/*
 * lfs_mflash.h
 *
 * Adapted from the Embedded Artists SDK's littlefs_shell example
 * (boards/evkcmimxrt1060/littlefs_examples/littlefs_shell/lfs_mflash.h,
 * confirmed real, from the SDK actually installed for this board). The
 * reference version pulled LittleFS_config/LITTLEFS_START_ADDR from a
 * Config-Tools-generated peripherals.h (an "mflash" component this
 * project doesn't have set up) -- defined directly here instead.
 */

#ifndef _LFS_MFLASH_H_
#define _LFS_MFLASH_H_

#include "lfs.h"
#include "mflash_drv.h"

/* 1MB into flash -- this project's actual compiled firmware image is
 * only ~44KB (confirmed from the linker map, _image_size), so this
 * leaves generous headroom before any collision, without needing to
 * coordinate with MCUboot slot partitioning (not set up in this
 * project yet -- see OTA_MCUBOOT_INTEGRATION.md, a later stage). */
#define LITTLEFS_START_ADDR 0x100000u

/* 1MB partition, 4KB blocks (= MFLASH_SECTOR_SIZE, one littlefs "block"
 * per erasable flash sector) = 256 blocks. */
#define LITTLEFS_BLOCK_COUNT 256u

struct lfs_mflash_ctx
{
    uint32_t start_addr;
};

extern struct lfs_mflash_ctx LittleFS_ctx;
extern const struct lfs_config LittleFS_config;

int lfs_mflash_read(const struct lfs_config *lfsc, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size);
int lfs_mflash_prog(const struct lfs_config *lfsc, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size);
int lfs_mflash_erase(const struct lfs_config *lfsc, lfs_block_t block);
int lfs_mflash_sync(const struct lfs_config *lfsc);

extern int lfs_get_default_config(struct lfs_config *lfsc);
extern int lfs_storage_init(const struct lfs_config *lfsc);

#endif
