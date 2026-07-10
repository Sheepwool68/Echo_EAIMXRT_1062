/*
 * rfid_logic.h
 *
 * Hardware-independent logic ported from ActiveRFID.C.
 * No Rabbit port I/O, no Dynamic C library calls -- safe to compile
 * and unit test on any platform, and to drop into the RT1062 project
 * once the surrounding I/O (FAT/FatFs, sockets, SPI) is wired up.
 */

#ifndef RFID_LOGIC_H
#define RFID_LOGIC_H

#include <stdint.h>
#include <stddef.h>
#include "nrf_record.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Builds the CSV line sent to the PC/socket client for one chip read.
 *
 * Original: CreateSockString()
 *
 * `out` must be at least 96 bytes. Returns the number of characters
 * written (as sprintf did), or a negative value on truncation.
 *
 * `channel` is Settings.Channel (0-based; caller adds 1 where the
 * original protocol expects a 1-based antenna/channel number).
 */
int rfid_create_sock_string(const nrf_record_t *entry,
                             int is_rewind,
                             char *out, size_t out_size,
                             uint32_t uhf_code_value,
                             int is_uhf,
                             uint8_t channel,
                             output_type_t output_type,
                             int trigger_time_active);

/*
 * Binary search of a fixed-record-size log file for the first record
 * whose date_time (rewind_type == REWIND_BY_TIME) or log_id
 * (rewind_type == REWIND_BY_RECNO) matches start_value.
 *
 * This version is I/O-abstracted: instead of calling fat_Seek/fat_Read
 * directly (Rabbit FAT16 lib), it takes function pointers so it can be
 * driven by FatFs (f_lseek/f_read) on the RT1062, or by a mock in unit
 * tests.
 *
 * seek_fn(ctx, absolute_offset) -> 0 on success, negative on error
 * read_fn(ctx, buf, size) -> bytes read, negative on error
 * file_size_fn(ctx) -> file size in bytes, or negative on error
 *
 * Returns:
 *   1  if a starting position was found; *out_record_no is set and the
 *      file position (via seek_fn) is left at that record.
 *   0  if start_value is beyond the last record (nothing to rewind).
 *  <0  on I/O error.
 *
 * If start_value == 0, *out_record_no is set to 0 and the file is
 * seeked to the start (full rewind), matching original behaviour.
 */
typedef int (*rfid_seek_fn)(void *ctx, long offset);
typedef long (*rfid_read_fn)(void *ctx, void *buf, size_t size);
typedef long (*rfid_filesize_fn)(void *ctx);

int rfid_binary_search_log(void *file_ctx,
                            rfid_seek_fn seek_fn,
                            rfid_read_fn read_fn,
                            rfid_filesize_fn file_size_fn,
                            uint32_t start_value,
                            rewind_type_t rewind_type,
                            uint32_t *out_record_no);

/*
 * Parses a dotted-quad IP address string (e.g. "192.168.1.90") into
 * 4 bytes. Returns 1 on success (exactly 4 valid octets), 0 on
 * malformed input.
 *
 * Original: parseIP_check() -- but with the UI/network side-effects
 * (updating Settings, bringing the interface down) stripped out. The
 * caller is responsible for acting on a successful parse.
 */
int rfid_parse_ip(const char *ip_string, uint8_t out_ip[4]);

/*
 * Rounds n up to the next value that is n's leading digit incremented,
 * followed by zeros -- e.g. 47 -> 50, 123 -> 200, 999 -> 1000.
 * (Only meaningful/used for n >= 10 in the original; n < 10 returns n
 * rounded up to the next power-of-ten boundary at the ones digit,
 * consistent with the original implementation.)
 *
 * Original: round_up_to_max_pow10()
 */
int rfid_round_up_to_max_pow10(int n);

#ifdef __cplusplus
}
#endif

#endif /* RFID_LOGIC_H */
