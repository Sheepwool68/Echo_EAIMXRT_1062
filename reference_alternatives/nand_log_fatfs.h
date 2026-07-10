/*
 * nand_log_fatfs.h
 *
 * ==========================================================================
 * SCAFFOLD -- NOT COMPILED OR TESTED IN THIS ENVIRONMENT (no FatFs available
 * here), same caveats as the other *_rt1062.c / *_lwip.c hardware glue files
 * in this port.
 *
 * STORAGE MEDIUM ASSUMPTION: this assumes FatFs is running over SD/eMMC via
 * the RT1062's USDHC + SDMMC middleware, which is the standard MCUXpresso
 * pairing and the most common choice for this kind of always-growing log
 * workload. If your board instead uses raw QSPI/OSPI NOR flash for storage,
 * the FatFs API calls in this file stay the same (FatFs abstracts the
 * medium via a diskio.c glue layer you'd write once, separately), BUT:
 * raw NOR flash has no wear-leveling of its own, and FatFs alone doesn't
 * provide any either. For a device that's continuously appending
 * timestamped records for the device's whole service life, littlefs
 * (which does wear-level) would be the more appropriate choice on NOR --
 * worth confirming which storage chip is actually on your board before
 * committing to this file's approach.
 * ==========================================================================
 *
 * Wraps the standard FatFs API (ff.h) to reconstruct the behavior of
 * MountNANDLogFile()/OpenNANDLogFile()/SaveRAMToNAND()/etc. (see the
 * porting notes -- their real implementation was in EchoFilesystem.lib,
 * not available to port directly), and wires the binary-search module
 * from rfid_logic.h to real f_lseek/f_read calls.
 */

#ifndef NAND_LOG_FATFS_H
#define NAND_LOG_FATFS_H

#include "ff.h"
#include "nand_log_logic.h"
#include "rfid_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FIL file;
    FATFS fs;
    int mounted;
    int open;
    uint64_t max_size_bytes; /* log rotation threshold, drives percent_full */
    char path[64];
} nand_log_t;

/* Was MountNANDLogFile(). drive_path e.g. "0:" for the first FatFs volume;
 * log_path e.g. "0:/ACTIVERFID.LOG". max_size_bytes sets the rotation
 * threshold used by nand_log_check_percent_full(). */
int nand_log_mount(nand_log_t *log, const char *drive_path,
                    const char *log_path, uint64_t max_size_bytes);

int nand_log_open_for_append(nand_log_t *log); /* was OpenNANDLogFile(1) */
int nand_log_open_for_read(nand_log_t *log);   /* was OpenNANDLogFile(0) */
int nand_log_close(nand_log_t *log);           /* was CloseNANDLogFile() */

/* Was SaveRAMToNAND(buf, count) -- appends. Caller must have called
 * nand_log_open_for_append() first. */
int nand_log_append_records(nand_log_t *log, const nrf_record_t *records, size_t count);

int nand_log_get_file_size(nand_log_t *log, uint64_t *out_size);     /* was GetNANDFileSize() */
int nand_log_get_last_log_id(nand_log_t *log, uint32_t *out_log_id); /* was GetLastLogID() */
int nand_log_check_percent_full(nand_log_t *log, int *out_percent);  /* was CheckNANDFileSize() */
int nand_log_reset(nand_log_t *log);                                  /* was ResetNANDLogFile() */

/*
 * Runs the binary search from rfid_logic.h against this log file over
 * real FatFs calls. Leaves the file positioned at the found record on
 * success, matching the original BinarySearch()'s contract.
 */
int nand_log_binary_search(nand_log_t *log, uint32_t start_value,
                            rewind_type_t rewind_type, uint32_t *out_record_no);

#ifdef __cplusplus
}
#endif

#endif /* NAND_LOG_FATFS_H */
