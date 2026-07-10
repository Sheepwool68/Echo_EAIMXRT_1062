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

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_RT1062_H */
