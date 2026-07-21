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
 *     (write 1 = reader ON, write 0 = reader OFF). CONFIRMED, per
 *     explicit clarification 2026-07-17: this switches the UHF reader
 *     MODULE's power supply ON/OFF only -- it has no relationship to
 *     antenna detection (uhf_reader_set_antennae()'s DC-continuity/
 *     return-loss checks, done entirely over the LPUART8 protocol link
 *     once the module already has power) on any board, current or
 *     future. Noted explicitly so the two don't get conflated just
 *     because they're both "about the UHF reader."
 *
 * Both pins moved together on every real on/off transition -- use
 * reader_power_set() below for that, not the two per-pin primitives
 * separately -- UNTIL 2026-07-17: on this particular board, READER_PWR
 * is now repurposed to drive the button LED instead (see
 * button_led_rt1062.c, which calls reader_pwr_set() directly, entirely
 * independent of reader_shutdown_set()/reader_power_set()) -- this
 * board has no real reader-power-control circuit connected to that pin
 * anyway. reader_power_set() below now only writes READER_SHUTDOWN. A
 * future board WITH real reader power control would need
 * reader_power_set() restored to writing both pins together AND the
 * LED moved off READER_PWR to a different pin.
 */

#ifndef READER_SHUTDOWN_RT1062_H
#define READER_SHUTDOWN_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

void reader_shutdown_rt1062_init(void);

/* Per-pin primitives. reader_shutdown_set() is used both directly (see
 * below) and via reader_power_set(); reader_pwr_set() is now called
 * directly ONLY by button_led_rt1062.c on this board (see this file's
 * own header comment) -- do not also call it from anywhere doing real
 * reader-power-control, since reader_power_set() no longer includes
 * it. */
void reader_shutdown_set(int enable); /* active low: enable=1 drives pin low */
void reader_pwr_set(int enable);      /* active high: enable=1 drives pin high */

/* Was the combined PB4 (+ READER_PWR) write inside the GENIE_SYSTEM
 * handler. CHANGED 2026-07-17 (see this file's header comment): now
 * only writes READER_SHUTDOWN -- enable=1 drives it low (was: "reader
 * fully on"), enable=0 drives it high (was: "reader fully off"). This
 * is what app_uhf_active_mode_toggle() calls. */
void reader_power_set(int enable);

#ifdef __cplusplus
}
#endif

#endif /* READER_SHUTDOWN_RT1062_H */
