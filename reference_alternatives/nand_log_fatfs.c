/*
 * nand_log_fatfs.c
 *
 * See nand_log_fatfs.h for scope/caveats (SCAFFOLD, not compiled here,
 * assumes SD/eMMC storage medium -- read the header comment first).
 */

#include "nand_log_fatfs.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Binary-search adapters: bind rfid_logic.h's generic seek/read/size  */
/* function pointers to real FatFs calls against a FIL*.                */
/* ------------------------------------------------------------------ */

static int fatfs_seek_adapter(void *ctx, long offset)
{
    FIL *f = (FIL *)ctx;
    FRESULT res = f_lseek(f, (FSIZE_t)offset);
    return (res == FR_OK) ? 0 : -1;
}

static long fatfs_read_adapter(void *ctx, void *buf, size_t size)
{
    FIL *f = (FIL *)ctx;
    UINT br = 0;
    FRESULT res = f_read(f, buf, (UINT)size, &br);
    if (res != FR_OK) {
        return -1;
    }
    return (long)br;
}

static long fatfs_filesize_adapter(void *ctx)
{
    FIL *f = (FIL *)ctx;
    return (long)f_size(f);
}

/* ------------------------------------------------------------------ */

int nand_log_mount(nand_log_t *log, const char *drive_path,
                    const char *log_path, uint64_t max_size_bytes)
{
    FRESULT res;

    memset(log, 0, sizeof(*log));
    strncpy(log->path, log_path, sizeof(log->path) - 1);
    log->max_size_bytes = max_size_bytes;

    res = f_mount(&log->fs, drive_path, 1); /* 1 = mount now, not lazily */
    if (res != FR_OK) {
        return -1;
    }
    log->mounted = 1;

    /* Ensure the log file exists so later size/read operations don't
     * have to special-case "never created yet" separately from "empty". */
    {
        FIL f;
        res = f_open(&f, log->path, FA_OPEN_ALWAYS | FA_WRITE);
        if (res != FR_OK) {
            return -1;
        }
        f_close(&f);
    }

    return 0;
}

int nand_log_open_for_append(nand_log_t *log)
{
    FRESULT res;
    if (log->open) {
        return -1; /* already open -- caller bug, mirrors original's implicit single-handle assumption */
    }
    /* FA_OPEN_APPEND (seeks to EOF automatically) requires FatFs R0.12+.
     * Using the portable FA_OPEN_ALWAYS + manual seek-to-end here so
     * this works on older FatFs versions too; switch to FA_OPEN_APPEND
     * if your FatFs version supports it and you'd rather rely on it. */
    res = f_open(&log->file, log->path, FA_OPEN_ALWAYS | FA_WRITE);
    if (res != FR_OK) {
        return -1;
    }
    res = f_lseek(&log->file, f_size(&log->file));
    if (res != FR_OK) {
        f_close(&log->file);
        return -1;
    }
    log->open = 1;
    return 0;
}

int nand_log_open_for_read(nand_log_t *log)
{
    FRESULT res;
    if (log->open) {
        return -1;
    }
    res = f_open(&log->file, log->path, FA_READ);
    if (res != FR_OK) {
        return -1;
    }
    log->open = 1;
    return 0;
}

int nand_log_close(nand_log_t *log)
{
    FRESULT res;
    if (!log->open) {
        return 0; /* matches CloseNANDLogFile() being safe to call defensively */
    }
    res = f_close(&log->file);
    log->open = 0;
    return (res == FR_OK) ? 0 : -1;
}

int nand_log_append_records(nand_log_t *log, const nrf_record_t *records, size_t count)
{
    size_t i;
    UINT bw;
    FRESULT res;

    if (!log->open) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        res = f_write(&log->file, &records[i], sizeof(nrf_record_t), &bw);
        if (res != FR_OK || bw != sizeof(nrf_record_t)) {
            return -1; /* partial/failed write -- caller should treat the
                          whole batch as failed; a torn trailing record is
                          handled gracefully by nand_log_record_count_from_size()
                          truncating it away on the next read, but better
                          to know the write failed at all */
        }
    }

    /* f_sync rather than waiting for f_close to flush -- records need
     * to survive a power loss between write bursts, and the original
     * closed the file after every burst (OpenNANDLogFile/CloseNANDLogFile
     * around each SaveRAMToNAND call) which had the same effect. If you
     * switch to keeping the file open continuously for performance
     * (recommended -- see nand_log_fatfs.h), keep this f_sync() call so
     * you don't lose that power-loss safety property. */
    res = f_sync(&log->file);
    return (res == FR_OK) ? 0 : -1;
}

int nand_log_get_file_size(nand_log_t *log, uint64_t *out_size)
{
    if (log->open) {
        *out_size = (uint64_t)f_size(&log->file);
        return 0;
    } else {
        FILINFO fno;
        FRESULT res = f_stat(log->path, &fno);
        if (res != FR_OK) {
            return -1;
        }
        *out_size = (uint64_t)fno.fsize;
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
        UINT br;
        FRESULT res;

        /* Round down to the last COMPLETE record in case of a torn
         * trailing write, matching nand_log_record_count_from_size()'s
         * truncate-down behavior. */
        uint64_t record_count = nand_log_record_count_from_size(size);
        uint64_t last_offset = nand_log_offset_for_record(record_count - 1);

        res = f_lseek(&log->file, (FSIZE_t)last_offset);
        if (res != FR_OK) {
            rc = -1;
        } else {
            res = f_read(&log->file, &rec, sizeof(rec), &br);
            if (res != FR_OK || br != sizeof(rec)) {
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
    FRESULT res;

    if (log->open) {
        nand_log_close(log);
    }

    res = f_unlink(log->path);
    if (res != FR_OK && res != FR_NO_FILE) {
        return -1;
    }

    {
        FIL f;
        res = f_open(&f, log->path, FA_CREATE_ALWAYS | FA_WRITE);
        if (res != FR_OK) {
            return -1;
        }
        f_close(&f);
    }
    return 0;
}

int nand_log_binary_search(nand_log_t *log, uint32_t start_value,
                            rewind_type_t rewind_type, uint32_t *out_record_no)
{
    if (!log->open) {
        return -1; /* caller must open_for_read first, matching the
                       original's fat_Open() call inside
                       RewindLogFile_BinarySearch/Remote_GetRecordNoByDate */
    }
    return rfid_binary_search_log(&log->file,
                                   fatfs_seek_adapter,
                                   fatfs_read_adapter,
                                   fatfs_filesize_adapter,
                                   start_value, rewind_type, out_record_no);
}
