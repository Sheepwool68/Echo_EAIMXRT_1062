/*
 * buzzer_rt1062.h
 *
 * ==========================================================================
 * SCAFFOLD -- not compiled/tested against real hardware here, same tier as
 * the other simple GPIO-output drivers in this port (e.g. the CS/wake pins
 * in nrf_spi_transport_rt1062.c).
 * ==========================================================================
 *
 * Was `BitWrPortI(PBDR, &PBDRShadow, 1, 2)` (PB2) inside Beep(). Off is
 * CONFIRMED as the same pin driven low.
 */

#ifndef BUZZER_RT1062_H
#define BUZZER_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at startup. Configures the buzzer GPIO as a digital output,
 * initially off. */
void buzzer_rt1062_init(void);

/* Was BitWrPortI(PBDR, &PBDRShadow, 1, 2) -- drive the buzzer pin high. */
void buzzer_on(void);

/* CONFIRMED: the same pin driven low. */
void buzzer_off(void);

/* Was the raw pulse trains inline in UHF_Reader_Control()
 * (ACTIVERFID_V1.02_UHF.c lines 1515-1522 for the double-beep on
 * starting reading, lines 1540-1542 for the single beep on stopping):
 * direct `BitWrPortI(PBDR,...,2); msDelay(50);` toggles. CONFIRMED from
 * source these are completely unconditional -- unlike every other
 * Beep() call site in the original, which the CALLER gates on
 * Settings.Beeper before calling Beep() itself, these two specific
 * beeps bypass that check entirely and always sound. Blocks for
 * count*50ms + (count-1)*50ms, toggling ON,50ms,OFF,50ms,ON,... --
 * use this directly (not app_beep_n(), which is gated on
 * Settings.Beeper and queues a non-blocking pulse train -- neither
 * matches these two calls' real behavior) wherever
 * UHF_Reader_Control()'s beeps need replicating exactly. */
void buzzer_beep_n_blocking(int count);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_RT1062_H */
