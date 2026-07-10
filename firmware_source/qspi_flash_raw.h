/*
 * qspi_flash_raw.h
 *
 * ==========================================================================
 * SCAFFOLD -- same tier as nand_log_flash_qspi.c (confirmed chip: ISSI
 * IS25WP128, 256B pages, 4KB sectors, standard opcodes -- see that file's
 * original header notes for the full derivation).
 * ==========================================================================
 *
 * REFACTOR NOTE: these primitives were originally private (static) inside
 * nand_log_flash_qspi.c. Extracted here so the MCUboot secondary-slot
 * writer (fw_install_mcuboot.c) can reuse the exact same tested
 * program/erase code path rather than duplicating it -- two independent
 * copies of "how to talk to this flash chip" is a maintenance hazard if
 * either ever needs a timing fix. nand_log_flash_qspi.c now calls these
 * instead of its own former static copies; behavior is unchanged.
 */

#ifndef QSPI_FLASH_RAW_H
#define QSPI_FLASH_RAW_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define QSPI_FLASH_PAGE_SIZE    256u
#define QSPI_FLASH_SECTOR_SIZE  4096u
#define QSPI_FLASH_TOTAL_SIZE   0x1000000u
#define QSPI_FLASH_AMBA_BASE    0x60000000u

/* Call once at startup before using the functions below -- sets up
 * whatever FlexSPI LUT sequences program/erase depend on. TODO: wire
 * to your actual FLEXSPI_Init()/FLEXSPI_UpdateLUT() calls, same as
 * documented in nand_log_flash_qspi.c's original notes. */
int qspi_flash_raw_init(void);

/* Reads len bytes starting at addr (absolute flash offset, not
 * relative to any partition) via the memory-mapped XIP region. */
int qspi_flash_read(uint32_t addr, uint8_t *out, size_t len);

/* Programs len bytes starting at addr, internally chunking at page
 * boundaries (NOR flash page-program can't cross a page boundary).
 * addr does NOT need to be page-aligned; len does NOT need to be a
 * multiple of the page size. The target region must already be
 * erased (0xFF) -- NOR flash can only clear bits via program, not set
 * them; call qspi_flash_erase_sector() first if unsure. */
int qspi_flash_program(uint32_t addr, const uint8_t *data, size_t len);

/* Erases the 4KB sector containing addr (addr is rounded down to the
 * nearest sector boundary internally). */
int qspi_flash_erase_sector(uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* QSPI_FLASH_RAW_H */
