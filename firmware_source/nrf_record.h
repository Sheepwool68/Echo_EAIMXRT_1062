/*
 * nrf_record.h
 *
 * Portable (non-Rabbit) definitions for the RFID Timing "ActiveRFID"
 * data record and related structures.
 *
 * Ported from ActiveRFID.C (Dynamic C / RCM6700) to standard C for
 * NXP i.MX RT1062 + MCUXpresso SDK.
 *
 * NOTES ON THE PORT:
 *  - Dynamic C's `char` was typically unsigned 8-bit; NXP/ARM `char` is
 *    implementation-defined (usually signed). We use fixed-width stdint
 *    types everywhere a specific bit width mattered in the original.
 *  - Dynamic C packs struct fields tightly by default in most contexts
 *    used here (no padding for these small char/long mixes matter for
 *    on-wire/on-disk layout). On ARM/GCC we must be explicit with
 *    __attribute__((packed)) wherever the struct is written to a file,
 *    or sent over SPI, or compared byte-for-byte with old records,
 *    otherwise struct padding will corrupt the FAT log file format
 *    and the SPI framing to the nRF52833.
 */

#ifndef NRF_RECORD_H
#define NRF_RECORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Output type for chip code formatting (Settings.OutputType) */
typedef enum {
    OUTPUT_DEC = 0,
    OUTPUT_HEX = 1
} output_type_t;

/* Rewind-while-reading state machine states (was RWRState) */
typedef enum {
    RWR_STOPPED  = 0,
    RWR_STARTING = 1,
    RWR_READING  = 2
} rwr_state_t;

/* Rewind type: 6 = by record number (LogID), 8 = by date/time, 9 = remote */
typedef enum {
    REWIND_BY_RECNO   = 6,
    REWIND_BY_TIME    = 8,
    REWIND_BY_TIME_REMOTE = 9
} rewind_type_t;

/*
 * On-disk / on-wire record produced by the nRF52833 base radio and
 * logged to the NAND/FAT log file. Field order and width MUST match
 * the original Dynamic C struct exactly if you need to read log files
 * created by the old firmware; otherwise you're free to reorder for
 * natural alignment as long as you update both the FAT log reader and
 * writer together.
 *
 * Kept packed + in original field order here so old log files remain
 * readable if migrated to the new hardware.
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t date_time;      /* seconds since epoch used by this firmware */
    uint16_t ms;              /* milliseconds within the second */
    int8_t   max_RSSI;
    uint8_t  wake_count;
    uint8_t  battery;
    uint16_t loop_data;
    char     xpdr_code[6];    /* NOT null-terminated; 6 raw bytes */
    uint8_t  has_been_sent;
    uint32_t log_id;
} nrf_record_t;
#pragma pack(pop)

/* NRF settings command payload (bt_adv/playback/chip programming etc) */
#pragma pack(push, 1)
typedef struct {
    uint8_t bt_adv;
    uint8_t playback;
    uint8_t chip_program;
    uint8_t chip_sleep;
    char    chip_code[6];
} nrf_settings_cmd_t;
#pragma pack(pop)

/* Persisted device settings block (was struct TSettings) */
typedef struct {
    int      init;
    uint8_t  rabbit_ip[4];
    uint8_t  beeper;
    uint8_t  reader_power;
    int      time_zone;
    int      add30;
    uint8_t  auto_set_gps_time;
    uint8_t  channel;
    uint8_t  remote_type;      /* 0=off, 1=GPRS/4G modem, 2=LAN */
    uint8_t  gprs_server_ip1[4];
    uint16_t gprs_server_port;
    char     apn_name[30];
    char     apn_user[30];
    char     apn_password[30];
    uint32_t nand_current_rec;
    uint32_t ram_current_rec;
    uint8_t  data_on_request;
    uint32_t last_time_sent;
    uint8_t  trigger_on;
    uint32_t gprs_current_rec;
    uint8_t  rabbit_gateway[4];
    uint8_t  rabbit_dns[4];
    uint8_t  send_data_to_remote_server;
    uint8_t  gprs_server_ip2[4];
    uint8_t  gprs_server_ip3[4];
    int      first_boot;
    uint8_t  use_dhcp;
    uint8_t  brightness;
    uint8_t  dim;
    uint8_t  uhf_region;       /* 0=FCC, 1=ETSI, 2=Aus */
    uint8_t  system;           /* 0=Active, 1=UHF */
    int      shutdown_status;
    uint8_t  output_type;      /* output_type_t */
} device_settings_t;

#define NORMAL_SHUTDOWN   0
#define ABNORMAL_SHUTDOWN 1

#ifdef __cplusplus
}
#endif

#endif /* NRF_RECORD_H */
