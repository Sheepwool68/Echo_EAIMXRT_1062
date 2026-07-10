/*
 * fw_install_mcuboot.h
 *
 * Writes a downloaded, pre-signed firmware image into MCUboot's
 * secondary (OTA staging) slot, using the shared flash primitives
 * from qspi_flash_raw.h. This is the sink you plug into
 * fw_downloader.h's fw_download_firmware() call.
 *
 * ARCHITECTURE CONTEXT: per the mcuboot_opensource example's
 * flash_partitioning.h (see OTA_MCUBOOT_INTEGRATION.md for the exact
 * values -- they depend on your build, not something this file
 * hardcodes), the 16MB flash is divided into: [bootloader][primary
 * slot][secondary slot][free space, where littlefs log storage lives].
 * This module writes into the secondary slot only -- MCUboot itself
 * (running before your app, on the next reset) does the actual
 * validation and swap into the primary slot.
 *
 * IMAGE SIGNING HAPPENS ON YOUR BUILD MACHINE, NOT HERE: the bytes
 * this module writes must already be a complete, imgtool-signed
 * MCUboot image (header + your app + TLV/signature trailer) -- that's
 * a build-time step (imgtool sign ...), not something the device does.
 * This module just writes the bytes it's given, faithfully, into the
 * right place.
 *
 * FLAGGED HIGHEST-UNCERTAINTY POINT IN THIS MODULE: fw_mcuboot_install_finalize()
 * calls into MCUboot's own public API to mark the new image pending,
 * rather than hand-writing the image trailer bytes ourselves (far
 * riskier to get subtly wrong). The exact function name has varied
 * across MCUboot versions (older: boot_set_pending(), newer:
 * boot_set_pending_multi() for multi-image-aware builds) -- verify
 * against your actual SDK's boot/bootutil/include/bootutil/bootutil_public.h
 * once you have it, rather than trusting the declaration here. This
 * fails SAFE if wrong: a missing/mismatched function is a build-time
 * link error, not a runtime hazard -- you'll know immediately, not
 * after a botched update in the field.
 */

#ifndef FW_INSTALL_MCUBOOT_H
#define FW_INSTALL_MCUBOOT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*mcuboot_flash_erase_fn)(uint32_t addr);
typedef int (*mcuboot_flash_program_fn)(uint32_t addr, const uint8_t *data, size_t len);

typedef struct {
    uint32_t slot_offset;
    uint32_t slot_size;
    uint32_t write_cursor;
    uint32_t erased_through;
    mcuboot_flash_erase_fn erase_fn;
    mcuboot_flash_program_fn program_fn;
} fw_mcuboot_install_ctx_t;

/* Wires erase_fn/program_fn explicitly, so this is testable with mocks
 * on the host (see test_fw_install_mcuboot.c) without needing real
 * flash. For real use, pass qspi_flash_erase_sector/qspi_flash_program
 * directly -- their signatures already match these function pointer
 * types exactly. */
void fw_mcuboot_install_init(fw_mcuboot_install_ctx_t *ctx,
                              uint32_t slot_offset, uint32_t slot_size,
                              mcuboot_flash_erase_fn erase_fn,
                              mcuboot_flash_program_fn program_fn);

/*
 * Matches fw_download_sink_fn's signature from fw_downloader.h --
 * pass &ctx as sink_ctx and this function as sink when calling
 * fw_download_firmware(). Erases sectors just-in-time as the write
 * cursor advances into them (never re-erasing an already-erased
 * sector), and fails closed (returns nonzero) if the image would
 * overflow the slot rather than writing past its end.
 */
int fw_mcuboot_install_sink(void *ctx, const uint8_t *data, size_t len);

/*
 * Call once after fw_download_firmware() returns FW_DL_OK and you've
 * independently verified progress.crc32 matches what you expected --
 * marks the image in the secondary slot as pending, so MCUboot
 * attempts to boot it on the next reset. See the file header's flagged
 * uncertainty note before trusting this in production.
 */
int fw_mcuboot_install_finalize(fw_mcuboot_install_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* FW_INSTALL_MCUBOOT_H */
