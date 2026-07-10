/*
 * reader_shutdown_rt1062.h
 *
 * Controls the UHF reader's two power-related pins, which must always
 * be toggled together (confirmed): READER_SHUTDOWN (was PB4 in the
 * GENIE_SYSTEM touch-event handler -- `BitWrPortI(PBDR, &PBDRShadow,
 * 1/0, 4)`) and READER_PWR (a second, related pin not directly visible
 * in the pasted GENIE_SYSTEM snippet, but confirmed alongside it).
 *
 * CONFIRMED pins and polarity:
 *   READER_SHUTDOWN = GPIO_SD_B0_03 = GPIO3 pin 15, ACTIVE LOW
 *     (write 0 = reader ON, write 1 = reader OFF/shutdown -- despite
 *     the original's own comment text saying "Hi - turn off reader"
 *     AND "Hi - turn on reader" for the respective 1-write and
 *     0-write lines, a copy-paste artifact in the original comment,
 *     not the actual logic -- the surrounding if/else branch purpose
 *     is what confirms which is which, not the comment text).
 *   READER_PWR = GPIO_SD_B0_05 = GPIO3 pin 17, ACTIVE HIGH
 *     (write 1 = reader ON, write 0 = reader OFF).
 *
 * Both pins move together on every real on/off transition -- use
 * reader_power_set() below for that, not the two per-pin primitives
 * separately, to avoid ever leaving them desynced.
 */

#ifndef READER_SHUTDOWN_RT1062_H
#define READER_SHUTDOWN_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

void reader_shutdown_rt1062_init(void);

/* Per-pin primitives -- prefer reader_power_set() below for normal
 * use; these are exposed in case you ever genuinely need independent
 * control, but per your instruction both pins should move together. */
void reader_shutdown_set(int enable); /* active low: enable=1 drives pin low */
void reader_pwr_set(int enable);      /* active high: enable=1 drives pin high */

/* Was the combined PB4 (+ READER_PWR) write inside the GENIE_SYSTEM
 * handler. enable=1 turns the reader fully on (SHUTDOWN low, PWR
 * high); enable=0 turns it fully off (SHUTDOWN high, PWR low). This
 * is what app_uhf_active_mode_toggle() calls. */
void reader_power_set(int enable);

#ifdef __cplusplus
}
#endif

#endif /* READER_SHUTDOWN_RT1062_H */
