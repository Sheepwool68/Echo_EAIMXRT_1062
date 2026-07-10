/*
 * uhf_crc.h
 *
 * Ported from UHF_READER.LIB's crctable/TM_CalcCRC/TM_AddCRC.
 *
 * This is the standard nibble-lookup CRC-CCITT (poly 0x1021, init
 * 0xFFFF) -- recognizable from the 16-entry table -- so the algorithm
 * itself is a well-understood, faithful port with high confidence.
 *
 * Preserves an original detail worth being explicit about: the CRC is
 * calculated starting from buf[1], NOT buf[0] -- the leading 0xFF sync
 * byte is always excluded from the checksum.
 */

#ifndef UHF_CRC_H
#define UHF_CRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Computes the CRC over buf[1..len-1] (buf[0], the sync byte, is
 * excluded, matching the original TM_CalcCRC exactly).
 */
uint16_t uhf_calc_crc(const uint8_t *buf, size_t len);

/*
 * Appends a 2-byte big-endian CRC at buf[len] and buf[len+1], computed
 * over buf[1..len-1]. buf must have at least len+2 bytes of space.
 * Was TM_AddCRC(buf, Len).
 */
void uhf_add_crc(uint8_t *buf, size_t len);

/*
 * Verifies a received frame's trailing CRC. frame_len is buf[1] (the
 * reader's own data-length field); the CRC is expected at
 * buf[frame_len+5] (MSB) and buf[frame_len+6] (LSB), matching
 * TM_ProcessChip's `crc != (buf[datalength+5]<<8 | buf[datalength+6])`
 * check. Returns 1 if valid, 0 if not.
 */
int uhf_verify_crc(const uint8_t *buf, uint8_t frame_data_length);

#ifdef __cplusplus
}
#endif

#endif /* UHF_CRC_H */
