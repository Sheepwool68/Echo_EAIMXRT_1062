/*
 * lpuart5_console_rt1062.h
 *
 * NEW CODE, not a port -- LPUART5/FTDI USB-serial has never been
 * exercised before (per explicit instruction). Pin muxing is confirmed
 * real (board/pin_mux.c: TX on GPIO_B1_12, RX on GPIO_B1_13), and the
 * 80MHz clock root is confirmed from board/clock_config.c
 * (UART_CLK_ROOT.outFreq, shared by all LPUART instances) -- but there
 * is no prior working reference for this specific interface, unlike
 * every other peripheral in this port. Treat this as first-time bring-up,
 * not a confirmed-working config carried over from anywhere.
 */

#ifndef LPUART5_CONSOLE_RT1062_H
#define LPUART5_CONSOLE_RT1062_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at startup. Initializes LPUART5 at 115200 8N1 (standard
 * terminal default -- not confirmed against any specific FTDI
 * configuration, just the common default). */
void lpuart5_console_rt1062_init(void);

/* Blocking write. */
void lpuart5_console_write(const uint8_t *buf, size_t len);

/* Non-blocking read from the interrupt-driven RX ring buffer -- returns
 * the number of bytes copied into buf (0 if nothing available). */
size_t lpuart5_console_read(uint8_t *buf, size_t max_len);

#ifdef __cplusplus
}
#endif

#endif /* LPUART5_CONSOLE_RT1062_H */
