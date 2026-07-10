/*
 * uhf_chip_array.h
 *
 * Ported from UHF_READER.LIB's Chips[]/TM_AddToChipArray()/
 * TM_ProcessChipArray(). This array tracks distinct tag reads over a
 * rolling window: when the same chip is seen multiple times within
 * UHF_CHIP_AGING_SECONDS, the entries are deduplicated into one record
 * (keeping the best RSSI, in principle -- see the flagged bug below);
 * once a chip's last-seen time falls outside that window, it's flushed
 * to a log record.
 *
 * See the porting-notes discussion for the RSSI-update bug found in
 * the original: uhf_chip_array_add() exposes both the faithful
 * (bug-preserved) and fixed behavior via the fix_rssi_update_bug flag.
 */

#ifndef UHF_CHIP_ARRAY_H
#define UHF_CHIP_ARRAY_H

#include <stdint.h>
#include <stddef.h>
#include "nrf_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Was the hardcoded '+3' in `if (Chips[i].Seconds + 3 <= tm_now)` */
#define UHF_CHIP_AGING_SECONDS 3

/* Was MAX_CHIP_ARRAY_SIZE */
#define UHF_CHIP_ARRAY_SIZE 400

/* Was the hardcoded 128-entry local buffer size in TM_ProcessChipArray,
 * which silently capped how many aged-out chips could be flushed in a
 * single call (any beyond this stay in the array and get picked up on
 * a subsequent call -- not a data-loss bug given how frequently this
 * runs, just a documented batch limit). */
#define UHF_CHIP_FLUSH_BATCH_MAX 128

typedef struct {
    uint32_t chip_code;    /* 0 = empty slot (was ChipCode) */
    int rssi;               /* negative dBm (was RSSI) */
    uint32_t seconds;       /* was Seconds */
    uint16_t ms;             /* was MilliSeconds */
    uint16_t antenna;        /* was Antenna (unsigned int, memcpy'd whole into
                                 loop_data -- kept 2 bytes wide for fidelity even
                                 though real antenna values are small, 1-4) */
    uint8_t reads;           /* was Reads */
    uint8_t has_been_sent;   /* was HasBeenSent */
    uint32_t ms_timer_time;  /* was MSTimerTime */
} uhf_chip_entry_t;

typedef enum {
    UHF_CHIP_IGNORED_ZERO_CODE = 0, /* chip_code == 0, matches original's guard */
    UHF_CHIP_ADDED_NEW,             /* first sighting; slot populated */
    UHF_CHIP_REREAD,                /* matches an existing entry; Reads incremented.
                                        Whether RSSI/antenna/timestamp were refreshed
                                        depends on fix_rssi_update_bug -- see header note. */
    UHF_CHIP_ARRAY_FULL,            /* new chip, but no free slot -- read is dropped,
                                        matching the original's silent-drop behavior
                                        (iArrayIndex stays -1, nothing is stored) */
} uhf_chip_add_result_t;

/*
 * Adds or updates a chip reading in the array.
 *
 * fix_rssi_update_bug:
 *   0 -- faithful to the original: on a re-read with a stronger RSSI,
 *        iOverwriteChip is computed but the field-update code path
 *        never actually runs (see porting notes) -- RSSI/antenna/
 *        timestamp stay frozen at the first reading; only Reads
 *        increments.
 *   1 -- fixed: a re-read with a stronger (less negative) RSSI DOES
 *        update RSSI/antenna/seconds/ms, matching what the variable
 *        name and comment ("Remember RSSI is negative...") suggest
 *        was actually intended.
 *
 * *io_chip_count / *io_unique_chips mirror the original's global
 * iChipCount/iUniqueChips counters -- pass the same variables across
 * calls if you want that running total preserved.
 */
uhf_chip_add_result_t uhf_chip_array_add(uhf_chip_entry_t *chips, size_t count,
                                          uint32_t chip_code, int rssi, uint16_t antenna,
                                          uint32_t chip_seconds, uint32_t chip_ms,
                                          int fix_rssi_update_bug,
                                          uint32_t *io_chip_count,
                                          uint32_t *io_unique_chips,
                                          size_t *out_index);

/*
 * Converts one chip entry to a log/socket record, matching the field
 * mapping in TM_ProcessChipArray exactly:
 *   - chip_code's low 4 bytes go into xpdr_code[2..5] (bytes [0..1]
 *     left zero) -- this is the same "byte 0 == 0 means UHF code"
 *     convention already relied on elsewhere in this port (see
 *     CreateSockString's is_uhf_code detection).
 *   - max_RSSI is stored POSITIVE (RSSI negated), matching the
 *     original's "RSSI saved as positive, for historical reasons"
 *     comment.
 *   - battery is always 0 (not applicable to UHF reads).
 * log_id is NOT set here -- that's the caller's job (was iLastLogID++
 * per flushed record in the original), matching how log_id assignment
 * is handled as an app-layer concern throughout this port.
 */
void uhf_chip_entry_to_record(const uhf_chip_entry_t *chip, nrf_record_t *out);

/*
 * Scans the array for entries whose age (now_seconds - entry.seconds)
 * has reached UHF_CHIP_AGING_SECONDS, converts up to max_out of them to
 * records (via uhf_chip_entry_to_record), assigns sequential log_ids
 * starting at log_id_start, and clears those slots (chip_code = 0) so
 * the slot becomes available again -- matching TM_ProcessChipArray.
 *
 * Returns the number of records written to out_records (and slots
 * cleared). If more than max_out entries are eligible for flushing,
 * only the first max_out (in array order) are processed this call --
 * the rest remain in the array and will be picked up on a later call.
 */
size_t uhf_chip_array_flush_aged(uhf_chip_entry_t *chips, size_t count,
                                  uint32_t now_seconds,
                                  nrf_record_t *out_records, size_t max_out,
                                  uint32_t log_id_start);

#ifdef __cplusplus
}
#endif

#endif /* UHF_CHIP_ARRAY_H */
