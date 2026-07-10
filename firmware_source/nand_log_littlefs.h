/*
 * nand_log_littlefs.h
 *
 * ==========================================================================
 * SCAFFOLD -- NOT COMPILED OR TESTED IN THIS ENVIRONMENT (no littlefs
 * available here), same caveats as the other hardware glue files in this
 * port. The littlefs API itself (lfs.h) is stable and well-documented
 * though, so this file is checked against it with reasonable confidence;
 * the genuinely uncertain part is the flash block-device layer it depends
 * on -- see nand_log_flash_qspi.h.
 * ==========================================================================
 *
 * Replaces nand_log_fatfs.h for QSPI NOR flash storage. Reconstructs the
 * same MountNANDLogFile()/OpenNANDLogFile()/SaveRAMToNAND()/etc. behavior
 * (see the porting notes from the FatFs version -- EchoFilesystem.lib's
 * real implementation wasn't available to port directly), now over
 * littlefs instead of FatFs.
 *
 * Deliberately takes a pre-built `const struct lfs_config *` rather than
 * knowing anything about the flash chip itself -- see
 * nand_log_flash_qspi.h/.c for the actual QSPI NOR read/prog/erase glue.
 */

#ifndef NAND_LOG_LITTLEFS_H
#define NAND_LOG_LITTLEFS_H

#include "lfs.h"
#include "nand_log_logic.h"
#include "rfid_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lfs_t lfs;
    lfs_file_t file;
    int mounted;
    int open;
    uint64_t max_size_bytes; /* log rotation threshold, drives percent_full */
    char path[64];
} nand_log_t;

/*
 * Was MountNANDLogFile(). cfg must already be fully populated (block
 * device geometry + read/prog/erase/sync callbacks) -- see
 * nand_log_flash_qspi_get_config(). Handles the "never formatted
 * before" case automatically (format-then-mount-again).
 */
int nand_log_mount(nand_log_t *log, const struct lfs_config *cfg,
                    const char *log_path, uint64_t max_size_bytes);

int nand_log_open_for_append(nand_log_t *log); /* was OpenNANDLogFile(1) */
int nand_log_open_for_read(nand_log_t *log);   /* was OpenNANDLogFile(0) */
int nand_log_close(nand_log_t *log);           /* was CloseNANDLogFile() */

/* Was SaveRAMToNAND(buf, count). Caller must have called
 * nand_log_open_for_append() first. */
int nand_log_append_records(nand_log_t *log, const nrf_record_t *records, size_t count);

int nand_log_get_file_size(nand_log_t *log, uint64_t *out_size);     /* was GetNANDFileSize() */
int nand_log_get_last_log_id(nand_log_t *log, uint32_t *out_log_id); /* was GetLastLogID() */
int nand_log_check_percent_full(nand_log_t *log, int *out_percent);  /* was CheckNANDFileSize() */
int nand_log_reset(nand_log_t *log);                                  /* was ResetNANDLogFile() */

/*
 * Runs the binary search from rfid_logic.h against this log file over
 * real littlefs calls. Leaves the file positioned at the found record
 * on success, matching the original BinarySearch()'s contract.
 */
int nand_log_binary_search(nand_log_t *log, uint32_t start_value,
                            rewind_type_t rewind_type, uint32_t *out_record_no);

/*
 * Seeks to a specific record index (0-based). Was
 * `fat_Seek(&NANDLogFile, Settings.GPRS_CurrentRec * sizeof(logEntry), SEEK_SET)`
 * in Remote_SendNextBatch(). Requires the log to already be open for
 * read.
 */
int nand_log_seek_to_record(nand_log_t *log, uint64_t record_index);

/*
 * Reads one record from the current file position and advances past
 * it. Was `fat_Read(&NANDLogFile, &logEntry, sizeof(logEntry))` in
 * Remote_SendNextBatchToSocket(). Returns 1 if a complete record was
 * read, 0 on EOF (or a short/torn trailing record -- matches the
 * original treating anything other than a full-size read as "no more
 * records"), negative on I/O error.
 */
int nand_log_read_next_record(nand_log_t *log, nrf_record_t *out);

#ifdef __cplusplus
}
#endif

#endif /* NAND_LOG_LITTLEFS_H */
