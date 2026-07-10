#include "fw_install_mcuboot.h"
#include "qspi_flash_raw.h"
#include <string.h>

void fw_mcuboot_install_init(fw_mcuboot_install_ctx_t *ctx,
                              uint32_t slot_offset, uint32_t slot_size,
                              mcuboot_flash_erase_fn erase_fn,
                              mcuboot_flash_program_fn program_fn)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->slot_offset = slot_offset;
    ctx->slot_size = slot_size;
    ctx->erase_fn = erase_fn;
    ctx->program_fn = program_fn;
}

int fw_mcuboot_install_sink(void *ctx_void, const uint8_t *data, size_t len)
{
    fw_mcuboot_install_ctx_t *ctx = (fw_mcuboot_install_ctx_t *)ctx_void;

    if ((uint64_t)ctx->write_cursor + len > ctx->slot_size) {
        return -1;
    }

    while (ctx->erased_through < ctx->write_cursor + (uint32_t)len) {
        uint32_t sector_abs_addr = ctx->slot_offset + ctx->erased_through;
        if (ctx->erase_fn(sector_abs_addr) != 0) {
            return -1;
        }
        ctx->erased_through += QSPI_FLASH_SECTOR_SIZE;
    }

    if (ctx->program_fn(ctx->slot_offset + ctx->write_cursor, data, len) != 0) {
        return -1;
    }
    ctx->write_cursor += (uint32_t)len;
    return 0;
}

/*
 * FLAGGED (see header): verify this declaration against your actual
 * SDK's bootutil_public.h before trusting it. Declared extern here
 * rather than #include-ing a header we don't have in this environment;
 * once you have the real mcuboot_opensource source, replace this with
 * `#include "bootutil/bootutil_public.h"` and remove the manual
 * declaration -- if the real signature differs, the compiler will tell
 * you immediately (a build error, not a runtime surprise).
 */
extern int boot_set_pending_multi(int image_index, uint8_t permanent);

int fw_mcuboot_install_finalize(fw_mcuboot_install_ctx_t *ctx)
{
    (void)ctx;
    return boot_set_pending_multi(0, 0);
}
