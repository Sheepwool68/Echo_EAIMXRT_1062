/*
 * uhf_packet_parser.h
 *
 * Ported from UHF_READER.LIB's TM_ProcessString/TM_ProcessChip/
 * TM_ProcessAntStatus/TM_ProcessRL.
 *
 * See the porting notes (two flagged anomalies in TM_ProcessString's
 * loop structure) -- this implements the single coherent scanning loop
 * that matches what the code actually executes, not the literal nested
 * loop as written.
 *
 * ANOTHER FIDELITY NOTE: TM_ReadSerialPort read into a fresh
 * stack-local buffer on every call (never persisted across calls), so
 * any tag-read frame split across two serial reads was silently and
 * completely lost in the original -- there was no carryover mechanism.
 * uhf_process_buffer() returns the number of bytes actually consumed,
 * which lets your caller optionally retain buf[consumed..len) and
 * prepend it to the next read (recommended -- avoids that data loss),
 * or discard it to match the original's behavior exactly. Your call.
 *
 * ds_rollover GATING NOTE: the original only processes tag-read frames
 * once `ds_rollover` is set (i.e. after the RTC's first tick, so tag
 * timestamps are meaningful) -- `if(ds_rollover) TM_ProcessChip(...)`.
 * That's app-level time-sync state, not parser state, so it's NOT
 * baked into this module. uhf_process_buffer() always parses and
 * reports tag-read events; your caller decides whether to act on them
 * based on its own time-sync status (see time_sync.h from this port).
 * IMPLEMENTED, 2026-07-15: app_loop.c's uhf_event_cb() gates its
 * UHF_FRAME_TAG_READ case on app->ds_rollover_seen (set once,
 * permanently, by process_time_sync() on the DS3231's first rollover),
 * matching the original's sticky ds_rollover check exactly.
 */

#ifndef UHF_PACKET_PARSER_H
#define UHF_PACKET_PARSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t chip_code;
    int rssi;            /* raw negative dBm value */
    int rssi_percent;    /* was RSSI_percent: (int)(RSSI*1.7+160), clamped to
                             a max of 100 but NOT clamped to a min of 0 in the
                             original -- can go negative for very weak reads
                             (e.g. RSSI <= -95ish). Preserved as-is; clamp
                             further in your caller if you want a strict
                             0-100 range for a gauge widget. */
    uint8_t antenna;
    uint8_t reads;
} uhf_tag_read_t;

typedef struct {
    uint8_t antenna;      /* 1-4, was buf[19] */
    uint8_t raw_value;    /* was buf[25] */
    int percent;          /* was RL_Percent */
    int good;             /* was the buf[25] < 100 "bad" check, inverted to "good" */
    uint8_t ant_bit;      /* bit to OR into your antenna-connected mask if good
                              (was `ants = ants | (k >> (ant_no-1))`, k fixed at 8) */
} uhf_return_loss_t;

typedef enum {
    UHF_FRAME_HEARTBEAT,
    UHF_FRAME_END_OF_ROUND,
    UHF_FRAME_START_CONFIRM,
    UHF_FRAME_RETURN_LOSS,
    UHF_FRAME_TAG_READ,
    UHF_FRAME_ANT_STATUS,
    UHF_FRAME_TEMPERATURE,
} uhf_frame_type_t;

typedef struct {
    uhf_frame_type_t type;
    union {
        uhf_tag_read_t tag;             /* UHF_FRAME_TAG_READ */
        uint8_t ant_status_mask;        /* UHF_FRAME_ANT_STATUS */
        uhf_return_loss_t return_loss;  /* UHF_FRAME_RETURN_LOSS */
        uint8_t temperature;            /* UHF_FRAME_TEMPERATURE, was readerTemp = p[5] */
    } data;
} uhf_frame_event_t;

/* Parses one tag-read frame (was TM_ProcessChip). Verifies the CRC
 * internally (reuses uhf_verify_crc). Returns 1 on success, 0 if the
 * CRC failed or the buffer was too short for the frame it claims to be. */
int uhf_parse_tag_read(const uint8_t *buf, size_t len, uhf_tag_read_t *out);

/* Parses one antenna-status frame (was TM_ProcessAntStatus). Returns
 * the 4-bit connected-antenna mask (bit3=ant1 .. bit0=ant4). */
uint8_t uhf_parse_ant_status(const uint8_t *buf, size_t len);

/* Parses one return-loss/VSWR test reply (was TM_ProcessRL). Returns
 * 1 on success, 0 if the buffer was too short. */
int uhf_parse_return_loss(const uint8_t *buf, size_t len, uhf_return_loss_t *out);

typedef void (*uhf_frame_event_cb)(void *cb_ctx, const uhf_frame_event_t *event);

/*
 * Scans buf[0..len) for recognized frames, calling cb once per
 * recognized frame (was TM_ProcessString's dispatch). Returns the
 * number of bytes consumed -- see the fidelity note above about
 * partial-frame carryover.
 */
size_t uhf_process_buffer(const uint8_t *buf, size_t len,
                           uhf_frame_event_cb cb, void *cb_ctx);

#ifdef __cplusplus
}
#endif

#endif /* UHF_PACKET_PARSER_H */
