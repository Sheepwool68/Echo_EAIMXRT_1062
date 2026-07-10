/*
 * gprs_records.h
 *
 * Ported from 4G_Modem.lib's TGPRSRec/TGPRSRec2 and the record-building
 * logic in Remote_SendNextBatchToSocket().
 *
 * WIDTH FIDELITY WARNING: see the porting notes -- every field that was
 * a plain Dynamic C `int` (16-bit) is explicitly int16_t/uint16_t here,
 * NOT `int` (which is 32-bit on ARM). Getting this wrong silently
 * corrupts the wire format for a remote server that still expects the
 * original 16-bit layout. Verified against the RecLen constants (28
 * and 30), which only add up correctly with 16-bit fields.
 */

#ifndef GPRS_RECORDS_H
#define GPRS_RECORDS_H

#include <stdint.h>
#include <stddef.h>
#include "nrf_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UHF-code variant (was TGPRSRec2) -- used when logEntry.xpdr_code[0]==0 */
#pragma pack(push, 1)
typedef struct {
    uint8_t  start_chr;       /* 0x02 */
    uint8_t  rec_len;         /* 28 -- data length only, not struct size */
    uint8_t  reader_no;       /* always 1 */
    uint32_t record_no;
    uint32_t chip_code;       /* numeric UHF code */
    uint32_t seconds;
    int16_t  ms;              /* was `int MilliSeconds` -- 16-bit on Dynamic C */
    uint8_t  mac_address[3];
    uint8_t  ultra_id;
    uint8_t  reader_time[8];  /* always zeroed in the original */
    uint8_t  antenna_no;
    uint8_t  end_chr;         /* 0x03 */
} gprs_rec_uhf_t;
#pragma pack(pop)

/* Active/LF-code variant (was TGPRSRec) -- used when logEntry.xpdr_code[0]!=0 */
#pragma pack(push, 1)
typedef struct {
    uint8_t  start_chr;       /* 0x02 */
    uint8_t  rec_len;         /* 30 -- data length only */
    uint8_t  reader_no;       /* always 1 */
    uint32_t record_no;
    char     chip_code[6];    /* raw 6-byte code string, not numeric */
    uint32_t seconds;
    int16_t  ms;
    uint8_t  mac_address[3];
    uint8_t  ultra_id;
    uint8_t  reader_time[8];
    uint8_t  antenna_no;      /* always 1 in this variant -- see notes below */
    uint8_t  end_chr;         /* 0x03 */
} gprs_rec_active_t;
#pragma pack(pop)

typedef enum {
    GPRS_RECORD_UHF,
    GPRS_RECORD_ACTIVE,
} gprs_record_kind_t;

/*
 * Builds the wire-format record for one log entry, matching
 * Remote_SendNextBatchToSocket()'s field population exactly, including
 * two real asymmetries between the two variants worth knowing about:
 *
 *  - ultra_id: the UHF variant uses the raw channel setting (0-based,
 *    a single global value); the Active variant derives it from the
 *    log entry's own loop_data bits (((loop_data>>6)&0x0F)+1),
 *    matching the per-timing-loop channel encoding used elsewhere in
 *    this port. These are genuinely different sources, not a
 *    copy-paste inconsistency -- UHF mode has one reader-wide channel,
 *    Active mode's loop_data already carries per-record channel info.
 *
 *  - antenna_no: the UHF variant copies logEntry.loop_data directly
 *    (TRUNCATED to a single byte -- loop_data is 16-bit, antenna_no is
 *    8-bit, so only the low byte survives; harmless in practice since
 *    real antenna numbers are small, but preserved as-is rather than
 *    silently "fixed"). The Active variant hardcodes antenna_no = 1
 *    unconditionally, regardless of the record's actual antenna.
 *
 * Returns the number of bytes written to out (sizeof(gprs_rec_uhf_t)
 * or sizeof(gprs_rec_active_t)), or 0 if out_size was too small.
 * *out_kind tells the caller which variant was used, in case the
 * caller wants to log/branch on it.
 */
size_t gprs_build_record(const nrf_record_t *log_entry, uint8_t channel,
                          const uint8_t mac_address[6], uint32_t record_no,
                          uint8_t *out, size_t out_size,
                          gprs_record_kind_t *out_kind);

#ifdef __cplusplus
}
#endif

#endif /* GPRS_RECORDS_H */
