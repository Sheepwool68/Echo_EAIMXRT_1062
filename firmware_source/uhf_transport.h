/*
 * uhf_transport.h
 *
 * Transport interface for the UHF reader's serial link (was Rabbit
 * serial port E: serEopen/serEwrite/serEread/serErdFlush/serEwrFlush).
 * On the RT1062 this maps to an LPUART.
 *
 * Same pattern as nrf_spi_transport.h: hardware calls behind a small
 * vtable so the orchestration logic (uhf_reader.h) is testable with a
 * mock, and the real LPUART glue is an isolated, swappable file.
 */

#ifndef UHF_TRANSPORT_H
#define UHF_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;

    /* Opens the UART at the given baud rate (was serEopen(115200)).
     * Returns 0 on success. */
    int (*open)(void *ctx, uint32_t baud_rate);

    /* Closes the UART (was serEclose -- referenced in the GENIE_SYSTEM
     * touch-event handler's UHF-off branch, alongside the
     * READER_SHUTDOWN pin assertion). Not previously in this vtable
     * since no pasted source had called for it until now. */
    void (*close)(void *ctx);

    /* Writes len bytes. Returns bytes written, or negative on error. */
    int (*write)(void *ctx, const uint8_t *buf, size_t len);

    /*
     * Reads up to max_len bytes, waiting up to timeout_ms for at least
     * one byte (was serEread(buf, size, timeout) -- the original's
     * timeout unit wasn't fully clear from the source alone; treat as
     * milliseconds and adjust if your board's serEread-equivalent used
     * a different unit). Returns bytes read (0 if the timeout elapsed
     * with nothing received), or negative on error.
     */
    int (*read)(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms);

    /* Discards any buffered received-but-unread bytes (was serErdFlush). */
    void (*flush_rx)(void *ctx);

    /* Waits for pending transmit data to finish sending (was serEwrFlush). */
    void (*flush_tx)(void *ctx);

    void (*delay_ms)(void *ctx, uint32_t ms);
} uhf_transport_t;

#ifdef __cplusplus
}
#endif

#endif /* UHF_TRANSPORT_H */
