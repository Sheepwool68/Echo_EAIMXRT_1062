/*
 * mflash_common.h
 *
 * Copied verbatim from the Embedded Artists SDK
 * (components/flash/mflash/mflash_common.h) -- confirmed real, from the
 * SDK actually installed for this board (2026-07-13).
 */

#ifndef __MFLASH_COMMON_H__
#define __MFLASH_COMMON_H__

#include <stdint.h>
#include <stdbool.h>

#include "fsl_common.h"

#define MFLASH_INVALID_ADDRESS (UINT32_MAX)

#define mflash_drv_is_page_aligned(x)   (((x) % (MFLASH_PAGE_SIZE)) == 0)
#define mflash_drv_is_sector_aligned(x) (((x) % (MFLASH_SECTOR_SIZE)) == 0)

/*
 * The addresses of FLASH locations used by APIs below may not correspond with the addresses space, especially when
 * FLASH remapping is being used. Use mflash_drv_phys2log/log2phys API to obtain actual pointer or physical address.
 */

int32_t mflash_drv_init(void);
int32_t mflash_drv_sector_erase(uint32_t sector_addr);
int32_t mflash_drv_page_program(uint32_t page_addr, uint32_t *data);
int32_t mflash_drv_read(uint32_t addr, uint32_t *buffer, uint32_t len);
void *mflash_drv_phys2log(uint32_t addr, uint32_t len);
uint32_t mflash_drv_log2phys(void *ptr, uint32_t len);

#endif
