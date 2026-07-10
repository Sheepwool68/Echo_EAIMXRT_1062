/*
 * nand_log_littlefs.c
 *
 * See nand_log_littlefs.h for scope/caveats.
 */

#include "nand_log_littlefs.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Binary-search adapters: bind rfid_logic.h's generic seek/read/size  */
/* function pointers to real littlefs calls against an lfs_file_t*.    */
/*                                                                      */
/* NOTE: these adapters need both the lfs_t* and the lfs_file_t* to    */
/* call littlefs's API, but rfid_binary_search_log()'s ctx is a single */
/* void*. We pass a small on-stack pair struct as ctx instead of just  */
/* the file pointer, unlike the FatFs version (where FIL alone was     */
/* enough because f_read/f_lseek don't take a separate FATFS* handle). */
/* ------------------------------------------------------------------ */

typedef struct {
    lfs_t *lfs;
    lfs_file_t *file;
} lfs_search_ctx_t;

static int lfs_seek_adapter(void *ctx, long offset)
{
    lfs_search_ctx_t *c = (lfs_search_ctx_t *)ctx;
    lfs_soff_t res = lfs_file_seek(c->lfs, c->file, (lfs_soff_t)offset, LFS_SEEK_SET);
    return (res >= 0) ? 0 : -1;
}

static long lfs_read_adapter(void *ctx, void *buf, size_t size)
{
    lfs_search_ctx_t *c = (lfs_search_ctx_t *)ctx;
    lfs_ssize_t n = lfs_file_read(c->lfs, c->file, buf, (lfs_size_t)size);
    return (n < 0) ? -1 : (long)n;
}

static long lfs_filesize_adapter(void *ctx)
{
    lfs_search_ctx_t *c = (lfs_search_ctx_t *)ctx;
    lfs_soff_t sz = lfs_file_size(c->lfs, c->file);
    return (sz < 0) ? -1 : (long)sz;
}

/* ------------------------------------------------------------------ */

int nand_log_mount(nand_log_t *log, const struct lfs_config *cfg,
                    const char *log_path, uint64_t max_size_bytes)
{
    int err;

    memset(log, 0, sizeof(*log));
    strncpy(log->path, log_path, sizeof(log->path) - 1);
    log->max_size_bytes = max_size_bytes;

    err = lfs_mount(&log->lfs, cfg);
    if (err != 0) {
        /* Standard littlefs boilerplate: mount fails with LFS_ERR_CORRUPT
         * (or similar) if the flash has never been formatted for
         * littlefs -- format it and mount again. This is the expected,
         * normal path on first boot / after a flash chip swap, not
         * necessarily an error condition worth alarming the user about. */
        err = lfs_format(&log->lfs, cfg);
        if (err != 0) {
            return -1;
        }
        err = lfs_mount(&log->lfs, cfg);
        if (err != 0) {
            return -1;
        }
    }
    log->mounted = 1;

    /* Ensure the log file exists, same reasoning as the FatFs version. */
    {
        lfs_file_t f;
        err = lfs_file_open(&log->lfs, &f, log->path, LFS_O_CREAT | LFS_O_WRONLY);
        if (err != 0) {
            return -1;
        }
        lfs_file_close(&log->lfs, &f);
    }

    return 0;
}

int nand_log_open_for_append(nand_log_t *log)
{
    int err;
    if (log->open) {
        return -1;
    }
    /* LFS_O_APPEND auto-seeks to EOF on every write -- simpler than the
     * FatFs version, which needed a manual seek-to-end for older FatFs
     * versions lacking FA_OPEN_APPEND. */
    err = lfs_file_open(&log->lfs, &log->file, log->path,
                         LFS_O_RDWR | LFS_O_CREAT | LFS_O_APPEND);
    if (err != 0) {
        return -1;
    }
    log->open = 1;
    return 0;
}

int nand_log_open_for_read(nand_log_t *log)
{
    int err;
    if (log->open) {
        return -1;
    }
    err = lfs_file_open(&log->lfs, &log->file, log->path, LFS_O_RDONLY);
    if (err != 0) {
        return -1;
    }
    log->open = 1;
    return 0;
}

int nand_log_close(nand_log_t *log)
{
    int err;
    if (!log->open) {
        return 0;
    }
    err = lfs_file_close(&log->lfs, &log->file);
    log->open = 0;
    return (err == 0) ? 0 : -1;
}

int nand_log_append_records(nand_log_t *log, const nrf_record_t *records, size_t count)
{
    lfs_ssize_t written;
    int err;

    if (!log->open) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }

    /* One call for the whole batch -- littlefs handles this fine and
     * it's fewer flash-program operations than one call per record. */
    written = lfs_file_write(&log->lfs, &log->file, records,
                              (lfs_size_t)(count * sizeof(nrf_record_t)));
    if (written < 0 || (size_t)written != count * sizeof(nrf_record_t)) {
        return -1;
    }

    /* Explicit sync so records survive a power loss between write
     * bursts -- same reasoning as the FatFs version's f_sync() call.
     * littlefs is power-loss-resilient by design (unlike FatFs, which
     * needs the caller to be careful about this), but a write that
     * hasn't been synced can still be lost if power drops before the
     * next automatic sync point, so this stays explicit rather than
     * relying on littlefs's internal buffering alone. */
    err = lfs_file_sync(&log->lfs, &log->file);
    return (err == 0) ? 0 : -1;
}

int nand_log_get_file_size(nand_log_t *log, uint64_t *out_size)
{
    if (log->open) {
        lfs_soff_t sz = lfs_file_size(&log->lfs, &log->file);
        if (sz < 0) {
            return -1;
        }
        *out_size = (uint64_t)sz;
        return 0;
    } else {
        struct lfs_info info;
        int err = lfs_stat(&log->lfs, log->path, &info);
        if (err != 0) {
            return -1;
        }
        *out_size = (uint64_t)info.size;
        return 0;
    }
}

int nand_log_get_last_log_id(nand_log_t *log, uint32_t *out_log_id)
{
    uint64_t size;
    int was_open = log->open;
    int rc = 0;

    if (nand_log_get_file_size(log, &size) != 0) {
        return -1;
    }

    if (size < sizeof(nrf_record_t)) {
        *out_log_id = 0; /* empty (or corrupt/truncated) log -- start fresh */
        return 0;
    }

    if (!was_open) {
        if (nand_log_open_for_read(log) != 0) {
            return -1;
        }
    }

    {
        nrf_record_t rec;
        lfs_ssize_t n;
        lfs_soff_t seek_res;

        /* Round down to the last COMPLETE record in case of a torn
         * trailing write. */
        uint64_t record_count = nand_log_record_count_from_size(size);
        uint64_t last_offset = nand_log_offset_for_record(record_count - 1);

        seek_res = lfs_file_seek(&log->lfs, &log->file, (lfs_soff_t)last_offset, LFS_SEEK_SET);
        if (seek_res < 0) {
            rc = -1;
        } else {
            n = lfs_file_read(&log->lfs, &log->file, &rec, sizeof(rec));
            if (n < 0 || (size_t)n != sizeof(rec)) {
                rc = -1;
            } else {
                *out_log_id = rec.log_id;
            }
        }
    }

    if (!was_open) {
        nand_log_close(log);
    }
    return rc;
}

int nand_log_check_percent_full(nand_log_t *log, int *out_percent)
{
    uint64_t size;
    if (nand_log_get_file_size(log, &size) != 0) {
        return -1;
    }
    *out_percent = nand_log_percent_full(size, log->max_size_bytes);
    return 0;
}

int nand_log_reset(nand_log_t *log)
{
    int err;

    if (log->open) {
        nand_log_close(log);
    }

    err = lfs_remove(&log->lfs, log->path);
    if (err != 0 && err != LFS_ERR_NOENT) {
        return -1;
    }

    {
        lfs_file_t f;
        err = lfs_file_open(&log->lfs, &f, log->path, LFS_O_CREAT | LFS_O_WRONLY | LFS_O_TRUNC);
        if (err != 0) {
            return -1;
        }
        lfs_file_close(&log->lfs, &f);
    }
    return 0;
}

int nand_log_binary_search(nand_log_t *log, uint32_t start_value,
                            rewind_type_t rewind_type, uint32_t *out_record_no)
{
    lfs_search_ctx_t ctx;

    if (!log->open) {
        return -1;
    }

    ctx.lfs = &log->lfs;
    ctx.file = &log->file;

    return rfid_binary_search_log(&ctx,
                                   lfs_seek_adapter,
                                   lfs_read_adapter,
                                   lfs_filesize_adapter,
                                   start_value, rewind_type, out_record_no);
}

int nand_log_seek_to_record(nand_log_t *log, uint64_t record_index)
{
    lfs_soff_t res;
    uint64_t offset;

    if (!log->open) {
        return -1;
    }
    offset = nand_log_offset_for_record(record_index);
    res = lfs_file_seek(&log->lfs, &log->file, (lfs_soff_t)offset, LFS_SEEK_SET);
    return (res >= 0) ? 0 : -1;
}

int nand_log_read_next_record(nand_log_t *log, nrf_record_t *out)
{
    lfs_ssize_t n;

    if (!log->open) {
        return -1;
    }
    n = lfs_file_read(&log->lfs, &log->file, out, sizeof(*out));
    if (n < 0) {
        return -1;
    }
    if ((size_t)n != sizeof(*out)) {
        return 0; /* EOF or torn trailing record -- treat as "no more records" */
    }
    return 1;
}
