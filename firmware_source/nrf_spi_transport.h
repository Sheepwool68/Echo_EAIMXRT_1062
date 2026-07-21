/*
 * nrf_spi_transport.h
 *
 * Hardware transport interface for the nRF52833 SPI link.
 *
 * The original Dynamic C code called SPIWrRd()/CS1_ENABLE/CS1_DISABLE
 * and polled a GPIO bit (PBDR bit 3) directly, all mixed in with the
 * protocol logic in comms_NRF(). This split isolates those hardware
 * calls behind a small vtable so:
 *
 *   1. The protocol logic (nrf_spi_protocol.c) can be unit tested on
 *      the host with a mock transport.
 *   2. The RT1062 LPSPI + GPIO implementation
 *      (nrf_spi_transport_rt1062.c) is a thin, isolated file that's
 *      the ONLY place that needs board-specific pin/instance numbers.
 *
 * IMPORTANT HARDWARE NOTE:
 * The original code busy-waited on the ready/attention line with NO
 * timeout ("while(BitRdPortI(PBDR, 3));"). That's a latent hang risk
 * -- if the nRF52833 glitches or resets mid-transaction, the RT1062
 * main loop would lock up forever. This port adds a timeout to every
 * wait; callers get an error back instead of hanging. Recommend
 * keeping this behaviour rather than reverting to the original
 * unbounded wait.
 */

#ifndef NRF_SPI_TRANSPORT_H
#define NRF_SPI_TRANSPORT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *hw_ctx; /* opaque, passed to every callback below */

    /* Assert / deassert chip-select to the nRF52833 (CS1 in the original). */
    void (*cs_assert)(void *hw_ctx);
    void (*cs_deassert)(void *hw_ctx);

    /*
     * Full-duplex SPI transfer of `len` bytes: writes tx[0..len) while
     * simultaneously capturing len received bytes into rx.
     * tx and rx may be the same buffer size but must not overlap.
     * Returns 0 on success, negative on transport/DMA error.
     */
    int (*transfer)(void *hw_ctx, const uint8_t *tx, uint8_t *rx, size_t len);

    /*
     * Blocks until the nRF ready/attention GPIO input reaches
     * `level` (0 or 1), or `timeout_ms` elapses.
     * Returns 0 if the level was observed, -1 on timeout.
     */
    int (*wait_ready_line)(void *hw_ctx, int level, uint32_t timeout_ms);

    /* Non-blocking read of the current ready/attention line state (0 or 1). */
    int (*read_ready_line)(void *hw_ctx);

    /* Short busy-wait delays used for CS setup/hold timing. */
    void (*delay_us)(void *hw_ctx, uint32_t us);
    void (*delay_ms)(void *hw_ctx, uint32_t ms);
} nrf_spi_transport_t;

/* Default timeout for the post-command "wait for ready line" step.
 * The original had no timeout at all; 500ms is a conservative starting
 * point -- tune once you've measured real nRF52833 response latency. */
#define NRF_SPI_READY_TIMEOUT_MS 500u

/* CS-to-clock setup delay. Original used an empty for(i=0;i<256;i++)
 * loop, whose real-world duration was never characterised in
 * microseconds from the C source alone. CONFIRMED 2026-07-17 by direct
 * oscilloscope measurement on the real, working Rabbit hardware: CS1
 * (nRF) goes low approximately 200us before the SPI clock transmission
 * begins for an nRF command -- this is real ground truth, not an
 * estimate. Bumped from 100us (an earlier guess, itself already bumped
 * once from a 10us placeholder on 2026-07-13) to 200us to match. */
#define NRF_SPI_CS_SETUP_DELAY_US 200u

#ifdef __cplusplus
}
#endif

#endif /* NRF_SPI_TRANSPORT_H */
