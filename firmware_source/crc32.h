/*
 * crc32.h
 *
 * Standard CRC-32 (IEEE 802.3 / zlib polynomial 0xEDB88320, reflected),
 * used to verify a downloaded firmware image arrived intact before any
 * bootloader-specific trust decision (MCUboot's own signature check,
 * or a bespoke dual-bank scheme's own verification -- either way, this
 * is a cheap first-line integrity check on the raw bytes).
 *
 * Not part of the original firmware -- new infrastructure for the OTA
 * update path. Supports incremental/streaming computation, since a
 * firmware image is too large to hold in RAM all at once on this class
 * of MCU.
 */

#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initial CRC32 state -- pass this as the running value to the first
 * crc32_update() call. */
#define CRC32_INITIAL 0xFFFFFFFFu

/* Feeds len bytes through the running CRC. Call repeatedly as data
 * arrives (e.g. once per received HTTP chunk); the return value is the
 * new running state to pass into the next call. */
uint32_t crc32_update(uint32_t running_crc, const uint8_t *data, size_t len);

/* Finalizes a running CRC into the standard output value (XORs with
 * 0xFFFFFFFF). Call once after all data has been fed via crc32_update(). */
uint32_t crc32_finalize(uint32_t running_crc);

/* Convenience one-shot for a buffer already fully in memory. */
uint32_t crc32_compute(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
