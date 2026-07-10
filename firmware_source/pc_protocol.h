/*
 * pc_protocol.h
 *
 * Portable re-implementation of the single-byte PC/socket command
 * protocol from ActiveRFID.C (CheckForPCCommands, SendReadingStatus,
 * SendDateTime, SendSettings, ProcessResetSocket's UDP reply, etc.)
 *
 * No socket calls here at all -- purely parsing incoming command
 * bytes and formatting outgoing message bytes. This is the layer to
 * unit test thoroughly, since it encodes your actual wire protocol
 * with RFIDServer/Outreach clients; get this right once and the
 * lwIP-specific plumbing around it becomes low-risk.
 *
 * As with the SPI protocol module: side effects that belong to the
 * application layer (actually starting/stopping the UHF reader,
 * actually rewinding the log file, actually persisting Settings) are
 * NOT performed here. This module tells you *which* command arrived
 * and *what its parsed fields are*; your app dispatches from there.
 */

#ifndef PC_PROTOCOL_H
#define PC_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "nrf_record.h" /* for rewind_type_t, output_type_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PC_CMD_UNKNOWN = 0,
    PC_CMD_REWIND,               /* '8' or '6' */
    PC_CMD_STOP_REWIND,          /* '9' */
    PC_CMD_UHF_STOP,             /* 'S' -- caller must also check ProgramState/Settings.System */
    PC_CMD_UHF_START,            /* 'R' -- caller must also check ProgramState/Settings.System */
    PC_CMD_START_LIVE_DATA,      /* '7' */
    PC_CMD_STOP_LIVE_DATA,       /* 's' */
    PC_CMD_SET_TIME,             /* 't' */
    PC_CMD_GET_TIME,             /* 'r' */
    PC_CMD_GET_READING_STATUS,   /* '?' */
    PC_CMD_GET_SETTINGS,         /* 'U' */
    PC_CMD_SET_SETTINGS,         /* 'u' -- unimplemented upstream too; classified for completeness */
    PC_CMD_REMOTE_CONFIG,        /* 0x03 */
    PC_CMD_BATTERY_QUERY,        /* 'B' -- no-op upstream currently */
    PC_CMD_SET_OUTPUT_TYPE,      /* 0x09, requires valid byte[1] */
    PC_CMD_MALFORMED,            /* recognised leading byte but payload didn't parse */
} pc_command_id_t;

typedef struct {
    rewind_type_t type;   /* REWIND_BY_RECNO (6) or REWIND_BY_TIME (8) */
    uint32_t from_time;
    uint32_t to_time;     /* 0 means "no upper bound", matches original */
} pc_rewind_params_t;

typedef struct {
    int hour, min, sec;
    int mday, mon;   /* mon as parsed 1-12, NOT zero-based (original doesn't zero-base it either) */
    int year;        /* full 4-digit year, e.g. 2018 */
} pc_datetime_fields_t;

typedef struct {
    uint32_t from_time; /* 0 means "use last-sent time", matching StartDataSend()'s fallback */
} pc_start_live_data_params_t;

typedef struct {
    pc_command_id_t id;
    union {
        pc_rewind_params_t rewind;
        pc_datetime_fields_t datetime;
        uint8_t output_type; /* OUTPUT_DEC or OUTPUT_HEX */
        pc_start_live_data_params_t start_live_data;
    } params;
} pc_parsed_command_t;

/*
 * Classifies and (where straightforward) fully parses an incoming
 * command buffer. Returns the same struct populated appropriately for
 * the identified command; params for commands with no fields (e.g.
 * PC_CMD_STOP_REWIND) are left zeroed/unused.
 *
 * If the leading byte is recognised but the payload fails to parse
 * (e.g. a rewind command with no CR-delimited to-time, or a 't'
 * command shorter than the required 21 bytes), returns
 * PC_CMD_MALFORMED rather than silently misinterpreting truncated
 * data -- the original had no such guard and would walk off the end
 * of the buffer searching for a '\r' that might not be there.
 */
pc_parsed_command_t pc_classify_command(const uint8_t *buf, size_t len);

/* Individual parsers, exposed separately in case you want to reuse them */
int pc_parse_rewind_command(const uint8_t *buf, size_t len, pc_rewind_params_t *out);
int pc_parse_set_datetime_command(const uint8_t *buf, size_t len, pc_datetime_fields_t *out);
int pc_validate_output_type(uint8_t value); /* 1 if OUTPUT_DEC or OUTPUT_HEX, else 0 */

/*
 * Was StartDataSend()'s payload parsing: skips the type char + 2
 * unused chars (split/antenna, matching the rewind command's layout),
 * then reads the from-time digits. GAP FIXED: this was originally
 * classified but never actually parsed in this module -- added while
 * wiring up main(), since the caller genuinely needs the from_time
 * value. Returns 1 on success (always succeeds if len >= 3; a from_time
 * of 0 is valid and means "use the last-sent time", matching the
 * original's fallback -- that substitution is the caller's job since
 * it needs Settings.iLastTimeSent, which this module doesn't own).
 */
int pc_parse_start_live_data_command(const uint8_t *buf, size_t len,
                                      pc_start_live_data_params_t *out);

/*
 * Workaround for a known RFIDServer quirk where an extra stray byte is
 * occasionally appended to the 't' (set time) command. Truncates to at
 * most max_len bytes, matching the original's temp_buf copy loop.
 * Returns the (possibly shortened) length.
 */
size_t pc_sanitize_command_length(const uint8_t *buf, size_t len, size_t max_len);

/* ---- Outgoing message formatters ---------------------------------- */

/* 'S=<reading><sending>\n' status reply, exactly 5 bytes, no NUL. */
void pc_build_reading_status(int is_reading, int send_data_active, uint8_t out[5]);

/* "HH:MM:SS DD-MM-YYYY (raw_seconds)\r\n" -- reply to 'r' (get time) */
int pc_format_datetime_reply(const pc_datetime_fields_t *t, long raw_seconds,
                              char *out, size_t out_size);

/* "V=<percent>\n" -- periodic battery status line */
int pc_format_battery_status(int batt_percent, char *out, size_t out_size);

/* "Connected,<last_time_sent>,U\n" -- sent once per new connection */
int pc_build_connect_greeting(unsigned long last_time_sent, char *out, size_t out_size);

/* "U<setting_id_byte><value>\n" formatters, matching SendSetting_Int/LongInt/Str.
 * Note setting_id is a raw byte code (e.g. 0x01, 0x02...), not necessarily
 * printable ASCII -- matches the original protocol exactly. */
int pc_format_setting_int(uint8_t setting_id, int value, char *out, size_t out_size);
int pc_format_setting_long(uint8_t setting_id, unsigned long value, char *out, size_t out_size);
int pc_format_setting_str(uint8_t setting_id, const char *value, char *out, size_t out_size);

/*
 * Builds the 38-byte UDP discovery reply (fixed prefix + "Ultra (Echo)"
 * + MAC as hex string). out must be at least 38 bytes.
 *
 * FIXED FROM ORIGINAL: the Dynamic C version relied on sprintf's NUL
 * terminator landing at buffer index 38 in a 38-byte array (valid
 * indices 0..37) -- a 1-byte overflow. This version writes exactly 38
 * meaningful bytes with no reliance on a trailing NUL.
 */
int pc_build_discovery_reply(const uint8_t mac[6], uint8_t *out, size_t out_size, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* PC_PROTOCOL_H */
