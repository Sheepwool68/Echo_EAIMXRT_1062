/*
 * gprs_config_record.h
 *
 * Ported from 4G_Modem.lib's TRemoteConfigRec and
 * Remote_CreateConfigRecord(). Same width-fidelity approach as
 * gprs_records.h -- every Dynamic C `int`/`unsigned int` field is
 * explicit int16_t/uint16_t here.
 *
 * FLAGGED DEAD CODE: the original sets RemoteConfigRec->ProgramState
 * TWICE -- once early from the raw ProgramState global, then
 * unconditionally overwritten a few lines later based on
 * Settings.System/is_reading. The first assignment is completely
 * dead (always overwritten before use). Only the net-effect (second
 * assignment) is ported.
 */

#ifndef GPRS_CONFIG_RECORD_H
#define GPRS_CONFIG_RECORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)
typedef struct {
    uint8_t  start_chr;         /* 0x03 */
    uint8_t  rec_len;           /* sizeof(this struct) */
    int16_t  ant11, ant12, ant13, ant14;
    int16_t  ant21, ant22, ant23, ant24; /* always 0 -- no second reader in this firmware */
    uint8_t  beeper;
    int16_t  reader_power[2];   /* [1] always 0 -- no second reader */
    int16_t  reader_mode[2];    /* always {3,3} -- "finish" mode hardcoded */
    uint32_t gating_interval;   /* always 3 -- hardcoded, not a real setting */
    uint8_t  gating_mode;       /* always 1 -- "per box", hardcoded */
    uint8_t  channel;           /* PARITY bit of (Settings.Channel+1), NOT the channel number -- see UltraID for that */
    uint16_t ultra_id;
    int16_t  time_zone;
    uint8_t  ant4_is_backup[2]; /* always {0,0} */
    uint32_t time;
    int16_t  battery_level;
    uint8_t  battery_type;      /* always 1 -- "percentage, not volts" */
    uint8_t  program_state;     /* see FLAGGED DEAD CODE note above */
    uint8_t  send_data_to_remote;
    uint8_t  rabbit_ip[4];
    uint8_t  mac_address[3];
    uint32_t no_chip_reads1;
    uint32_t no_chip_reads2;    /* always 0 -- single reader only */
    uint8_t  gps_coords[35];    /* [34] is overwritten with a fixed 0x02 device-type marker */
    uint8_t  end_chr[3];        /* {0x03, 0x0D, 0x0A} */
} gprs_remote_config_rec_t;
#pragma pack(pop)

typedef struct {
    uint8_t ants;                 /* antenna-connected bitmask, bit3=ant1..bit0=ant4 */
    uint8_t reader_power;         /* Settings.ReaderPower */
    uint8_t beeper;                /* Settings.Beeper */
    uint8_t channel;               /* Settings.Channel, 0-based */
    int16_t time_zone;             /* Settings.TimeZone */
    uint32_t rtc_seconds;          /* was read_rtc() */
    uint8_t send_data_to_remote;   /* Settings.SendDataToRemoteServer */
    uint8_t rabbit_ip[4];
    uint8_t mac_address[6];        /* full 6 bytes; builder uses [3..5] like the original */
    int is_reading;                /* ProgramState == READING */
    int uhf_system_mode;           /* Settings.System */
    uint8_t gps_coords[35];        /* first 34 bytes used; [34] is overwritten by the builder */
    int16_t battery_percent;
    uint32_t chip_reads;
} gprs_config_inputs_t;

void gprs_build_config_record(const gprs_config_inputs_t *in, gprs_remote_config_rec_t *out);

#ifdef __cplusplus
}
#endif

#endif /* GPRS_CONFIG_RECORD_H */
