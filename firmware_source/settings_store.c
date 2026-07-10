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
    lfs_ssize_t n;

    if (lfs_file_open(lfs, &f, SETTINGS_STORE_PATH, LFS_O_RDONLY) != LFS_ERR_OK) {
        return 0;
    }

    n = lfs_file_read(lfs, &f, &hdr, sizeof(hdr));
    if (n != (lfs_ssize_t)sizeof(hdr)) {
        lfs_file_close(lfs, &f);
        return 0;
    }

    n = lfs_file_read(lfs, &f, out, sizeof(*out));
    lfs_file_close(lfs, &f);

    if (n != (lfs_ssize_t)sizeof(*out)) {
        return 0;
    }

    return settings_store_validate(&hdr, out, sizeof(*out));
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
