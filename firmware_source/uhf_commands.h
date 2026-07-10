/*
 * uhf_commands.h
 *
 * Ported from UHF_READER.LIB's Open_Reader/TM_SetAntennae/
 * TM_InitialiseReader/StartReaders/StopReaders/GetReaderTemp.
 *
 * These are pure byte-buffer builders -- no serial I/O here. Given
 * inputs (antenna mask, region, channel, mode), each function fills a
 * caller-supplied buffer with the exact command bytes to send, and
 * returns the length. Your transport layer (uhf_reader_transport.h,
 * next) sends the bytes and reads the reply.
 *
 * VALIDATION NOTE: several commands in the original have their CRC
 * pre-baked as literal hex bytes in the source rather than computed at
 * runtime (e.g. the FCC/China full-spectrum commands). I checked three
 * of these against uhf_calc_crc() and they match exactly (0x4BBC,
 * 0x4BBB, 0xBDB8) -- strong independent validation of the CRC port
 * using real production data, beyond the hand-traced vectors in
 * uhf_crc's own tests. Every command below uses uhf_add_crc() at build
 * time rather than embedding pre-baked bytes, since we can now trust
 * it.
 *
 * FLAGGED: one region branch in the original (EU-high, region==4)
 * writes literal placeholder CRC bytes via sprintf and then
 * IMMEDIATELY overwrites them with TM_AddCRC() -- the literal bytes
 * are dead code, most likely copy-pasted from the China branch. Ported
 * as just "compute the CRC," matching what actually executes.
 */

#ifndef UHF_COMMANDS_H
#define UHF_COMMANDS_H

#include <stdint.h>
#include <stddef.h>

/*
 * GENERAL NOTE ON A RECURRING PATTERN: several commands in the original
 * call sprintf() with more "%c" format specifiers than arguments
 * supplied (e.g. 9 %c's, 7 values) -- undefined behavior in the strict
 * sense (reads an unsupplied vararg), but in every case except one
 * (uhf_build_set_dynamic_q) the extra byte(s) sit at the exact position
 * TM_AddCRC() overwrites immediately afterward, so the garbage value is
 * never actually used or transmitted. Read as: the developer left
 * placeholder slots for the CRC and undercounted the sprintf args to
 * match, not a functional bug. Ported as literal prefix + computed CRC
 * throughout, which sidesteps the UB entirely.
 */
#define UHF_DWELL_TIME_MS 40 /* was DWELL_TIME */

#ifdef __cplusplus
extern "C" {
#endif

/* Region codes, matching Settings.UHF_region's observed values (0-5) */
typedef enum {
    UHF_REGION_FCC = 0,
    UHF_REGION_EU = 1,
    UHF_REGION_AU = 2,
    UHF_REGION_CHINA = 3,
    UHF_REGION_EU_HIGH = 4,
    UHF_REGION_INDONESIA = 5,
} uhf_region_t;

/* Antenna output power levels seen (as alternately-commented-out
 * options) in TM_SetAntennae's power-set command. The 31.5dBm value
 * was the active one in the source; exposed as a parameter here
 * instead of silently hardcoding just that one. */
typedef enum {
    UHF_POWER_20DBM   = 0x07D0,
    UHF_POWER_31_5DBM = 0x0C4E, /* was the active default in the original */
    UHF_POWER_32DBM   = 0x0C80,
    UHF_POWER_32_5DBM = 0x0CB2,
} uhf_power_level_t;

/* Was GetSubcrc(): simple sum-of-bytes checksum (low byte of the sum),
 * used for StartReaders()'s inner sub-command checksum. */
uint8_t uhf_sub_checksum(const uint8_t *data, size_t len);

/* Was Open_Reader()'s version-query command. Writes 5 bytes. */
size_t uhf_build_get_version(uint8_t *out, size_t out_size);

/* Was TM_SetAntennae()'s antenna DC-connection check. Writes 6 bytes
 * (literal in the original, revalidated against uhf_calc_crc). */
size_t uhf_build_dc_check(uint8_t *out, size_t out_size);

/*
 * Was TM_SetAntennae()'s per-antenna return-loss/VSWR test command.
 * antenna: 1-4. Writes 27 bytes.
 */
size_t uhf_build_return_loss_test(uint8_t *out, size_t out_size,
                                   uhf_region_t region, uint8_t antenna);

/*
 * Was TM_SetAntennae()'s antenna-enable command, built from the
 * 4-bit connected-antenna mask (bit3=ant1..bit0=ant4, matching
 * uhf_parse_ant_status()'s output). Writes (cnt*2)+5 bytes where cnt
 * is the number of connected antennas (0 if ants==0, in which case
 * this returns 0 and writes nothing -- matches the original's `if(ants)`
 * guard). *out_duty_cycle receives the computed duty_cycle value (was
 * the global `duty_cycle = (cnt * DWELL_TIME) + 10`).
 */
size_t uhf_build_antenna_enable(uint8_t *out, size_t out_size,
                                 uint8_t ants_mask, uint32_t *out_duty_cycle);

/* Was TM_SetAntennae()'s power-set command. Writes 26 bytes. */
size_t uhf_build_power_set(uint8_t *out, size_t out_size, uhf_power_level_t power);

/* Was TM_InitialiseReader()'s "get current program" command. Writes 5 bytes. */
size_t uhf_build_get_program(uint8_t *out, size_t out_size);

/* Was TM_InitialiseReader()'s "firmware boot mode" command. Writes 5 bytes. */
size_t uhf_build_firmware_boot_mode(uint8_t *out, size_t out_size);

/* Was TM_InitialiseReader()'s "get supported regions" command. Writes 5 bytes. */
size_t uhf_build_get_regions(uint8_t *out, size_t out_size);

/*
 * Was TM_InitialiseReader()'s region/frequency-set command -- the
 * largest single piece of region-specific literal data in the library.
 * channel is only consulted for UHF_REGION_EU (selects between two
 * frequency-subset variants when channel > 7, matching the original's
 * "avoid clash for mats abutting two diff boxes" comment).
 */
size_t uhf_build_region_frequency(uint8_t *out, size_t out_size,
                                   uhf_region_t region, uint8_t channel);

/* Was TM_InitialiseReader()'s Q-algorithm-select command (dynamic Q).
 * Writes 8 bytes, fully static (no parameters affect it). */
size_t uhf_build_set_dynamic_q(uint8_t *out, size_t out_size);

/*
 * Was TM_InitialiseReader()'s target-mode/session/RF-mode sequence.
 * This is actually THREE separate commands sent back-to-back in the
 * original (target mode, session, RF mode), which differ depending on
 * uhf_mode (0 = start-line: single target A, session 1, RF Mode 13
 * max-sensitivity; nonzero = finish-line: single target A, session 0,
 * RF Mode 7 DRM-FCC). Fills out[] with all three commands
 * back-to-back and returns the total length; *out_command_count
 * receives how many individual commands were written (always 3),
 * so your transport layer can send+read-reply between each one
 * rather than blasting all three with no gap, matching the original's
 * serEwrite/msDelay/TM_ReadSerialPort sequencing between each command.
 * *out_command_lengths (must have space for 3) receives each
 * individual command's length.
 */
size_t uhf_build_mode_sequence(uint8_t *out, size_t out_size, int uhf_mode,
                                size_t out_command_lengths[3],
                                int *out_command_count);

/*
 * Was StartReaders()'s "Asynchronous Inventory" start command --
 * the reader stays in this mode indefinitely, streaming tag reads.
 * heartbeat_enabled selects between the original's active (no
 * heartbeat, byte value 0x20) and commented-out (with heartbeat,
 * byte value 0x80) variants -- exposed as a parameter rather than
 * silently picking one. Writes 24 bytes.
 */
size_t uhf_build_start_reading(uint8_t *out, size_t out_size, int heartbeat_enabled);

/* Was StopReaders()'s stop-inventory command. Writes 19 bytes. */
size_t uhf_build_stop_reading(uint8_t *out, size_t out_size);

/* Was GetReaderTemp() / the second command in StopReaders(). Writes 5 bytes. */
size_t uhf_build_get_temperature(uint8_t *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* UHF_COMMANDS_H */
