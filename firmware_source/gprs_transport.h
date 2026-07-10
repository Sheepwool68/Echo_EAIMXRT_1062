/*
 * gprs_transport.h
 *
 * Transport interface for the 4G modem's serial link + power/wake
 * GPIOs (was Rabbit serial port F: serFopen/serFwrite/serFread/
 * serFrdFlush/serFwrFlush/serFclose, plus PE2 wake and PA2 power-enable
 * GPIO pins). Same pattern as uhf_transport.h.
 */

#ifndef GPRS_TRANSPORT_H
#define GPRS_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;

    int (*open)(void *ctx, uint32_t baud_rate);
    void (*close)(void *ctx);
    int (*write)(void *ctx, const uint8_t *buf, size_t len);
    int (*read)(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms);
    void (*flush_rx)(void *ctx);
    void (*flush_tx)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);

    /* Was PE2: modem wake/power-key pin. level 0 = wake (LO), 1 = idle (HI). */
    void (*set_wake_pin)(void *ctx, int level);
    /* Was PA2: modem power-enable pin. level 1 = on, 0 = off. */
    void (*set_power_enable)(void *ctx, int level);
} gprs_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* GPRS_TRANSPORT_H */
