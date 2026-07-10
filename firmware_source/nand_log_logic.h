/*
 * nand_log_logic.h
 *
 * Pure, hardware-independent logic reconstructed from how
 * CheckNANDFileSize()/GetNANDFileSize()/ResetNANDLogFile() were USED
 * in ActiveRFID.C (their actual implementation is in EchoFilesystem.lib,
 * which wasn't provided -- see the porting notes at the top of this
 * conversation turn). No file I/O here; nand_log_fatfs.h wraps this
 * with real FatFs calls.
 */

#ifndef NAND_LOG_LOGIC_H
#define NAND_LOG_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include "nrf_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Percent-full calculation, 0-100, matching CheckNANDFileSize()'s
 * observed contract (drives both a gauge widget and the 97% auto-reset
 * threshold). Clamped to [0, 100] regardless of input.
 */
int nand_log_percent_full(uint64_t file_size_bytes, uint64_t max_size_bytes);

/*
 * Was `if(filesize >= 97) ResetNANDLogFile();` in UHF_Reader_Control().
 * Kept as a named, testable predicate rather than an inline magic
 * number comparison so the threshold is documented and can't silently
 * drift between call sites.
 */
#define NAND_LOG_AUTO_RESET_THRESHOLD_PERCENT 97
int nand_log_should_auto_reset(int percent_full);

/* Record count implied by a raw byte size, matching
 * `GetNANDFileSize() / sizeof(struct nrf_record)`. */
uint64_t nand_log_record_count_from_size(uint64_t file_size_bytes);

/* Byte offset of record index n (0-based) within the log file. */
uint64_t nand_log_offset_for_record(uint64_t record_index);

#ifdef __cplusplus
}
#endif

#endif /* NAND_LOG_LOGIC_H */
