/*
 * nrf_spi_protocol.h
 *
 * Portable re-implementation of comms_NRF() from ActiveRFID.C, split
 * into one function per command. Talks only through nrf_spi_transport_t
 * -- no direct SPI/GPIO/board calls -- so it's unit-testable and reusable
 * regardless of MCU.
 *
 * Deliberate differences from the original:
 *  - Every "wait for ready line" now has a timeout (see
 *    nrf_spi_transport.h). Functions return NRF_SPI_ERR_TIMEOUT instead
 *    of hanging.
 *  - Side effects that belong to the application layer, not the SPI
 *    protocol -- socket writes, NAND log writes, GENIE display updates,
 *    the beeper, chip_reads counter -- are NOT done here. Each retrieve
 *    function just returns parsed records; the caller (your main loop /
 *    a "record dispatch" module) is responsible for what happens to them.
 *    This mirrors good layering and makes both halves independently
 *    testable.
 *  - Record fields are parsed byte-by-byte with explicit little-endian
 *    reads rather than memcpy'd onto a struct. The original relied on
 *    the Rabbit compiler's struct layout matching the wire format
 *    exactly (including one undocumented padding byte -- see the
 *    protocol notes in the .c file). That's fragile across compilers/
 *    architectures; explicit field-at-a-time parsing removes the
 *    dependency on struct packing entirely while preserving the exact
 *    same wire offsets.
 */

#ifndef NRF_SPI_PROTOCOL_H
#define NRF_SPI_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "nrf_spi_transport.h"
#include "nrf_record.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NRF_SPI_OK             = 0,
    NRF_SPI_ERR_TIMEOUT    = -1,  /* ready line never reached expected state */
    NRF_SPI_ERR_TRANSFER   = -2,  /* transport->transfer() failed */
    NRF_SPI_ERR_TOO_MANY   = -3,  /* nRF reported more records than MAX_RECORDS */
    NRF_SPI_ERR_BUF_TOO_SMALL = -4, /* caller's output array too small */
} nrf_spi_status_t;

/* Max records retrievable in one poll/retrieve cycle (was MAX_RECORDS = 40) */
#define NRF_SPI_MAX_RECORDS 40
/* Live record wire frame size (was REC_LENGTH = 20) */
#define NRF_SPI_LIVE_FRAME_LEN 20
/* Playback record wire frame size (was hardcoded 7 in the original) */
#define NRF_SPI_PLAYBACK_FRAME_LEN 7

/* Sentinel byte meaning "ignore this transaction" (was 0xBB) */
#define NRF_SPI_POLL_IGNORE_SENTINEL 0xBBu

typedef enum {
    NRF_RECORD_TYPE_LIVE     = 1,
    NRF_RECORD_TYPE_PLAYBACK = 2,
} nrf_record_type_t;

/* -------------------------------------------------------------------
 * 0x01: Poll for data availability.
 *
 * On success (NRF_SPI_OK), *out_count and *out_record_type are valid.
 * If the nRF sent the "ignore" sentinel, returns NRF_SPI_OK with
 * *out_count == 0 -- caller should simply do nothing further, matching
 * original behaviour (this was silently swallowed before; now it's
 * visible to the caller via out_count == 0 rather than a hidden branch).
 * ---------------------------------------------------------------- */
nrf_spi_status_t nrf_spi_poll(const nrf_spi_transport_t *t,
                               uint8_t *out_count,
                               uint8_t *out_record_type);

/* -------------------------------------------------------------------
 * 0x02 (record_type == 1): Retrieve `count` live chip-read records.
 * Writes up to max_out parsed records into out[]; returns the number
 * actually parsed (>= 0), or a negative nrf_spi_status_t on error.
 * ---------------------------------------------------------------- */
int nrf_spi_retrieve_live_records(const nrf_spi_transport_t *t,
                                   uint8_t count,
                                   nrf_record_t *out,
                                   size_t max_out);

/* -------------------------------------------------------------------
 * 0x02 (record_type == 2): Retrieve `count` playback records.
 * out_code must be at least 7 bytes (6 chars + NUL).
 * Returns number of records parsed (>= 0), or negative status on error.
 * ---------------------------------------------------------------- */
int nrf_spi_retrieve_playback_records(const nrf_spi_transport_t *t,
                                       uint8_t count,
                                       nrf_record_t *out,
                                       size_t max_out,
                                       char out_code[7],
                                       uint32_t *out_boots,
                                       uint32_t *out_bt_time,
                                       uint8_t *out_fw_version);

/* 0x03: set LF reader RF power (Settings.ReaderPower, 20-100 in steps of 10) */
nrf_spi_status_t nrf_spi_set_reader_power(const nrf_spi_transport_t *t, uint8_t power_percent);

/* 0x04: set BT advertising on-time index (RFID.bt_adv, index into BT_ON_TIMES[]) */
nrf_spi_status_t nrf_spi_set_bt_advertising(const nrf_spi_transport_t *t, uint8_t bt_adv_index);

/*
 * 0x05/0x06: program a transponder's 6-byte code.
 * Computes the same XOR checksum as the original (seed 0xAA XORed with
 * each of the 6 code bytes) and returns it via out_crc for logging.
 */
nrf_spi_status_t nrf_spi_program_chip_code(const nrf_spi_transport_t *t,
                                            const char code[6],
                                            uint8_t *out_crc);

/* 0x07: set playback mode (RFID.playback) */
nrf_spi_status_t nrf_spi_set_playback_mode(const nrf_spi_transport_t *t, uint8_t playback);

/* 0x08/0x09: send current UTC time (seconds since epoch used by the nRF fw) */
nrf_spi_status_t nrf_spi_send_datetime(const nrf_spi_transport_t *t, uint32_t unix_time);

/* 0x0A: set station channel (Settings.Channel) */
nrf_spi_status_t nrf_spi_set_channel(const nrf_spi_transport_t *t, uint8_t channel);

/* 0x0B: set transponder low-power scanning mode (RFID.chip_sleep) */
nrf_spi_status_t nrf_spi_set_sleep_mode(const nrf_spi_transport_t *t, uint8_t sleep);

/* 0x0C: turn off the LF transmitter */
nrf_spi_status_t nrf_spi_transmitter_off(const nrf_spi_transport_t *t);

/* 0x0D: read li-ion battery ADC / percent */
nrf_spi_status_t nrf_spi_get_battery_percent(const nrf_spi_transport_t *t, uint8_t *out_percent);

/* 0x0E: read nRF52833 firmware version byte */
nrf_spi_status_t nrf_spi_get_fw_version(const nrf_spi_transport_t *t, uint8_t *out_version);

/* 0xFA: instruct the nRF52833 to enter DFU mode for 30 seconds */
nrf_spi_status_t nrf_spi_enter_dfu(const nrf_spi_transport_t *t);

#ifdef __cplusplus
}
#endif

#endif /* NRF_SPI_PROTOCOL_H */
