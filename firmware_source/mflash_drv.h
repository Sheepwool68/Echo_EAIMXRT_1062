/*
 * mflash_drv.h
 *
 * Adapted from the Embedded Artists SDK's
 * components/flash/mflash/evkcmimxrt1060/mflash_drv.h (the
 * evkcmimxrt1060 variant, not the generic mimxrt1062 one -- chosen
 * because its FLASH_SIZE constant, 0x4000 ("128Mb / 1KByte"), matches
 * this board's actual confirmed 16MB/128Mbit flash exactly, and its
 * command opcodes are the standard JEDEC set an SFDP-discoverable part
 * like the IS25WP128 supports, unlike the other variant's
 * vendor-specific opcodes). Sector/page sizes (4KB/256B) are standard
 * for this flash family, unchanged from the reference.
 */

#ifndef __MFLASH_DRV_H__
#define __MFLASH_DRV_H__

#include "mflash_common.h"

#ifndef MFLASH_SECTOR_SIZE
#define MFLASH_SECTOR_SIZE (0x1000)
#endif

#ifndef MFLASH_PAGE_SIZE
#define MFLASH_PAGE_SIZE (256)
#endif

#ifndef MFLASH_FLEXSPI
#define MFLASH_FLEXSPI (FLEXSPI)
#endif

#ifndef MFLASH_BASE_ADDRESS
#define MFLASH_BASE_ADDRESS (FlexSPI_AMBA_BASE)
#endif

#endif
