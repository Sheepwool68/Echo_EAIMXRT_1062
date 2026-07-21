/*
 * nand_log_flash_qspi.h
 *
 * ==========================================================================
 * SUPERSEDED, 2026-07-14 -- app_init.c's storage stage now uses
 * lfs_mflash.c/mflash_drv.c instead (NXP's own driver, adapted from the
 * real EA SDK's littlefs_shell example, CONFIRMED WORKING on real
 * hardware -- full mount/format/write/read-back round trip). That
 * driver targets the exact same physical chip this file was written
 * for; the difference was only ever which software drove it. This file
 * is kept in the tree (not deleted) since its FlexSPI-port research and
 * the MCUboot-aware flash_offset design intent (see below) may still be
 * useful once MCUboot partitioning actually exists -- but nothing
 * currently calls into it. See project memory for the fuller
 * comparison of the two.
 * ==========================================================================
 *
 * CONFIRMED CHIP: Embedded Artists iMX RT1062 OEM board (Rev C1,
 * EAC00428) ships with an ISSI IS25WP128 -- 128Mbit/16MByte QuadSPI NOR,
 * 256-byte pages, uniform 4/32/64KB erase blocks, standard JEDEC-compatible
 * command set (confirmed against the board datasheet and the IS25WP128
 * datasheet). Memory-mapped at 0x60000000-0x60FFFFFF on this board.
 *
 * This is now board-specific rather than a generic placeholder -- the
 * values in nand_log_flash_qspi.c (256B page, 4KB sector erase, standard
 * 0x02/0x06/0x05/0x20 opcodes) match this chip. Still SCAFFOLD in the sense
 * that it hasn't been compiled/tested in this environment (no SDK here),
 * and two things remain genuinely your call rather than something I can
 * determine remotely:
 *   1. Which FlexSPI port (A1 vs A2) the flash is wired to on this specific
 *      board -- defaulted to A1 (the conventional boot-flash port on
 *      RT1060-family designs) but worth a two-second check against the
 *      Embedded Artists carrier board schematic.
 *   2. How you want to partition the 16MB between firmware (this chip is
 *      also your XIP boot flash) and the littlefs log filesystem -- see
 *      flash_offset/flash_size below. I don't know your firmware image
 *      size, so this isn't something I can pick a sensible default for.
 * ==========================================================================
 *
 * Populates an lfs_config for littlefs to use over FlexSPI-attached QSPI
 * NOR flash. nand_log_littlefs.h consumes this as an opaque, pre-built
 * config and has no flash-chip-specific knowledge of its own.
 */

#ifndef NAND_LOG_FLASH_QSPI_H
#define NAND_LOG_FLASH_QSPI_H

#include "lfs.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Populates *out_cfg with block-device geometry and read/prog/erase/sync
 * callbacks wired to the IS25WP128 QSPI NOR flash via FlexSPI, plus
 * lookahead/cache buffers sized appropriately.
 *
 * flash_offset: byte offset within the 16MB chip where the littlefs
 * filesystem should live. Following the MCUboot integration decision
 * (see fw_install_mcuboot.h/OTA_MCUBOOT_INTEGRATION.md), this chip's
 * layout is now FOUR regions in order: [MCUboot bootloader][primary
 * app slot][secondary/OTA-staging slot][littlefs log storage] --
 * flash_offset must be set past the END of the secondary slot, not
 * just past the firmware image as originally noted here. The exact
 * boundary comes from your flash_partitioning.h (generated when you
 * set up the mcuboot_opensource example) -- this file has no
 * visibility into that layout on its own, so double-check the two
 * stay in sync if you ever resize the slots.
 * flash_size: total bytes littlefs is allowed to use (must be a multiple
 * of the 4KB sector size). Total chip capacity is 0x1000000 (16MB) --
 * flash_offset + flash_size must not exceed that.
 *
 * Returns 0 on success, negative on failure (e.g. flash_size not a whole
 * number of erase blocks).
 */
int nand_log_flash_qspi_get_config(struct lfs_config *out_cfg,
                                    uint32_t flash_offset,
                                    uint32_t flash_size);

#ifdef __cplusplus
}
#endif

#endif /* NAND_LOG_FLASH_QSPI_H */
