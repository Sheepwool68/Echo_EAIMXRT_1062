#include "settings_store.h"
#include "crc32.h"
#include <string.h>

void settings_store_build_header(settings_store_header_t *out_hdr,
                                  const void *settings_data, size_t settings_size)
{
    out_hdr->magic = SETTINGS_STORE_MAGIC;
    out_hdr->version = (uint16_t)SETTINGS_STORE_VERSION;
    out_hdr->size = (uint16_t)settings_size;
    out_hdr->crc32 = crc32_compute((const uint8_t *)settings_data, settings_size);
}

int settings_store_validate(const settings_store_header_t *hdr,
                             const void *settings_data, size_t settings_size)
{
    uint32_t computed_crc;

    if (hdr->magic != SETTINGS_STORE_MAGIC) {
        return 0;
    }
    if (hdr->version != SETTINGS_STORE_VERSION) {
        return 0;
    }
    if (hdr->size != (uint16_t)settings_size) {
        return 0;
    }

    computed_crc = crc32_compute((const uint8_t *)settings_data, settings_size);
    if (computed_crc != hdr->crc32) {
        return 0;
    }

    return 1;
}

int settings_store_load(lfs_t *lfs, device_settings_t *out)
{
    lfs_file_t f;
    settings_store_header_t hdr;
    device_settings_t tmp;
    lfs_ssize_t n;

    if (lfs_file_open(lfs, &f, SETTINGS_STORE_PATH, LFS_O_RDONLY) != LFS_ERR_OK) {
        return 0;
    }

    n = lfs_file_read(lfs, &f, &hdr, sizeof(hdr));
    if (n != (lfs_ssize_t)sizeof(hdr)) {
        lfs_file_close(lfs, &f);
        return 0;
    }

    /* FIXED 2026-07-20 -- was reading directly into `out` (the caller's
     * live app->settings) here, BEFORE validation below. That meant a
     * REJECTED file (bad magic/version/size/CRC) still clobbered the
     * caller's already-correct in-RAM defaults with the invalid file's
     * raw bytes as a side effect of this read -- the function's return
     * value correctly reported "invalid", but the damage was already
     * done, silently defeating app_init.c's own "on failure, the
     * defaults set above already apply" assumption. Confirmed on real
     * hardware: bumping SETTINGS_STORE_VERSION (meant to force exactly
     * this fallback-to-defaults path) had no effect on the corrupted
     * rabbit_ip because of this bug -- the stale file's zeroed rabbit_ip
     * got loaded into app->settings regardless of the version check
     * correctly rejecting it moments later. Now reads into a local
     * scratch buffer first and only copies to `out` if validation
     * actually passes -- a rejected file no longer touches the caller's
     * struct at all. */
    n = lfs_file_read(lfs, &f, &tmp, sizeof(tmp));
    lfs_file_close(lfs, &f);

    if (n != (lfs_ssize_t)sizeof(tmp)) {
        return 0;
    }

    if (!settings_store_validate(&hdr, &tmp, sizeof(tmp))) {
        return 0;
    }

    *out = tmp;
    return 1;
}

int settings_store_save(lfs_t *lfs, const device_settings_t *settings)
{
    lfs_file_t f;
    settings_store_header_t hdr;
    int err;

    settings_store_build_header(&hdr, settings, sizeof(*settings));

    err = lfs_file_open(lfs, &f, SETTINGS_STORE_PATH,
                         LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err != LFS_ERR_OK) {
        return -1;
    }

    if (lfs_file_write(lfs, &f, &hdr, sizeof(hdr)) != (lfs_ssize_t)sizeof(hdr)) {
        lfs_file_close(lfs, &f);
        return -1;
    }
    if (lfs_file_write(lfs, &f, settings, sizeof(*settings)) != (lfs_ssize_t)sizeof(*settings)) {
        lfs_file_close(lfs, &f);
        return -1;
    }

    return (lfs_file_close(lfs, &f) == LFS_ERR_OK) ? 0 : -1;
}
