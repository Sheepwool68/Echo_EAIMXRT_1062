/*
 * gprs_response_parser.h
 *
 * Ported from 4G_Modem.lib's GPRS_ReadSer (CSQ extraction only --
 * the actual serial read is transport I/O, not ported here),
 * ProcessRemoteConfigSettings(), and Remote_CheckForResponse()'s
 * dispatch logic.
 *
 * FIXED BUG (see conversation notes): the original's
 * `if(expected == "CSQ:")` compares POINTER IDENTITY, not string
 * content -- undefined/fragile behavior that likely only "worked" by
 * accident under Dynamic C's specific string-literal handling. Ported
 * using proper string comparison, since blindly preserving the pointer
 * comparison would silently break CSQ/signal-strength detection
 * entirely on a different compiler (the two literals are virtually
 * guaranteed to have different addresses under GCC).
 *
 * FIXED DOCSTRING: the original's GPRS_ReadSer comment says "1: found
 * but not expected, 2: found expected" -- backwards from what the code
 * actually does (and from how call sites elsewhere use it). Ported
 * matching the actual code behavior; see gprs_response_result_t.
 */

#ifndef GPRS_RESPONSE_PARSER_H
#define GPRS_RESPONSE_PARSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CSQ / generic AT response classification ---------------------- */

typedef enum {
    GPRS_RESP_NONE = 0,      /* nothing to classify (empty buffer) */
    GPRS_RESP_MATCHED = 1,   /* expected substring found (matches original's actual behavior) */
    GPRS_RESP_UNMATCHED = 2, /* buffer non-empty but expected substring not found */
} gprs_response_result_t;

/* Classifies an already-received, NUL-terminated buffer against an
 * expected substring. Does not touch the serial port. */
gprs_response_result_t gprs_classify_response(const char *received, const char *expected);

/*
 * Extracts and validates a CSQ (signal quality) value from a received
 * AT+CSQ response, e.g. "+CSQ: 15,99". Locates csq_marker (typically
 * "CSQ:") and reads the 2 ASCII digits starting 5 bytes after the
 * marker's start (matching the original's ptr[5]/ptr[6] offsets,
 * which assumes a single space between the marker and the digits).
 * Returns 1 and sets *out_csq if a valid value (0 < csq < 32) was
 * found; 0 otherwise (marker not found, or value out of range).
 */
int gprs_parse_csq(const char *received, const char *csq_marker, int *out_csq);

/* ---- ProcessRemoteConfigSettings ------------------------------------ */

typedef enum {
    RCFG_EVT_BEEPER_SET,           /* 0x21 -- APPLIED in the original */
    RCFG_EVT_SEND_TO_REMOTE_SET,   /* 0x2E -- APPLIED in the original */
    RCFG_EVT_STOP_READING,         /* 0x53 */
    RCFG_EVT_START_READING,        /* 0x52 */
    RCFG_EVT_REWIND,               /* 0x39 (remote) or 0x38 (local) */
    RCFG_EVT_NOOP,                 /* consumed but never applied in the original --
                                       see noop_command_byte for which one */
} rcfg_event_type_t;

typedef struct {
    rcfg_event_type_t type;
    uint8_t noop_command_byte;    /* RCFG_EVT_NOOP */
    uint8_t beeper_on;             /* RCFG_EVT_BEEPER_SET: 0 or 1 */
    uint8_t send_to_remote_value;  /* RCFG_EVT_SEND_TO_REMOTE_SET */
    int is_remote_rewind;          /* RCFG_EVT_REWIND: 1 for 0x39, 0 for 0x38 */
    const uint8_t *rewind_data;    /* RCFG_EVT_REWIND: pointer to the rewind command
                                       bytes, starting at the 0x38/0x39 byte -- feed
                                       directly to pc_parse_rewind_command() from the
                                       TCP module, since it's the same convention */
    size_t rewind_data_len;        /* RCFG_EVT_REWIND: bytes available from rewind_data
                                       (bounded by the buffer end, not necessarily the
                                       full rewind command -- pc_parse_rewind_command
                                       does its own bounds-safe CR search) */
} rcfg_event_t;

typedef void (*rcfg_event_cb)(void *cb_ctx, const rcfg_event_t *ev);

/*
 * Processes one config record's worth of data, starting at buf[0]
 * (expected to be the 0x03 start marker -- caller identifies this the
 * same way the original's Remote_CheckForResponse did before calling
 * ProcessRemoteConfigSettings). Fires cb once per recognized command.
 *
 * Returns the declared record length (iLen from the original) -- NOT
 * necessarily how many bytes were actually consumed; see
 * gprs_process_response_buffer's notes on the fixed-vs-actual skip
 * distance bug this was involved in.
 *
 * *out_mark_client_outreach is set to 1 if this record's declared
 * length was 0 (matching the original's `iClientType[iRewindSocket]=1`
 * side effect) -- the caller decides which socket index to mark, since
 * that's connection-management state this module doesn't own.
 */
int rcfg_process_buffer(const uint8_t *buf, size_t buf_len,
                         rcfg_event_cb cb, void *cb_ctx,
                         int *out_mark_client_outreach);

/* ---- Remote_CheckForResponse's top-level dispatch ------------------- */

typedef enum {
    GPRS_RX_CONFIG_RECORD,  /* leading 0x03 -- hand off to rcfg_process_buffer */
    GPRS_RX_OK_ACK,          /* "OK" + a record number */
} gprs_rx_event_type_t;

typedef struct {
    gprs_rx_event_type_t type;
    const uint8_t *config_record_data; /* GPRS_RX_CONFIG_RECORD: pointer into buf, at the 0x03 */
    size_t config_record_len;          /* GPRS_RX_CONFIG_RECORD: bytes remaining from that point */
    int ack_valid;                      /* GPRS_RX_OK_ACK: was atol(p) > 0 in the original */
    uint32_t ack_record_no;             /* GPRS_RX_OK_ACK: valid only if ack_valid -- already +1,
                                            matching the original's iGPRSBaseRecord = atol(p)+1 */
} gprs_rx_event_t;

typedef void (*gprs_rx_event_cb)(void *cb_ctx, const gprs_rx_event_t *ev);

/*
 * Scans a received buffer for 0x03 config records and "OK<n>"
 * acknowledgements, firing cb for each. `buf` must be readable up to
 * `len`; for the OK-ack case, the digit-parsing internally uses
 * strtol-style parsing bounded by `len`, unlike the original's raw
 * atol(p) (which relied on the buffer being NUL-terminated / safely
 * bounded some other way) -- this is a defensive improvement, not a
 * behavior change for well-formed input.
 *
 * FIXED BUG: the original advances its scan pointer by a FIXED 6
 * bytes after every config record, regardless of the record's own
 * declared length (which can be much longer, e.g. rewind sub-commands
 * up to ~50 bytes) -- very likely a bug that would misinterpret a long
 * record's tail bytes as new top-level commands. This port advances by
 * the record's actual declared length instead.
 */
void gprs_process_response_buffer(const uint8_t *buf, size_t len,
                                   gprs_rx_event_cb cb, void *cb_ctx);

#ifdef __cplusplus
}
#endif

#endif /* GPRS_RESPONSE_PARSER_H */
