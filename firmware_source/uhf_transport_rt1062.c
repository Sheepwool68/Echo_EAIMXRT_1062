/*
 * uhf_transport_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- LPUART instance now CONFIRMED (LPUART8). Still not compiled/
 * tested against real hardware here, same caveats as the other *_rt1062.c
 * files in this port.
 * ==========================================================================
 *
 * Implements uhf_transport_t (see uhf_transport.h) using LPUART in
 * interrupt-driven RX (ring buffer) + blocking TX mode. Was Rabbit serial
 * port E (serEopen/serEwrite/serEread/serErdFlush/serEwrFlush).
 */

#include "uhf_transport_rt1062.h"

#include "fsl_lpuart.h"
#include "fsl_common.h"
#include "fsl_clock.h" /* explicit now -- was relying on a transitive include via fsl_lpi2c.h that only existed in this port's host-test stub, not necessarily your real SDK's header chain */
#include <string.h>

/* CONFIRMED: LPUART8, wired to the UHF RFID reader's serial line. */
#define UHF_LPUART_BASE       LPUART8
#define UHF_LPUART_IRQn       LPUART8_IRQn
/* FIXED (2026-07-13): was CLOCK_GetFreq(kCLOCK_Usb1PllClk), an
 * unresolved TODO placeholder that would have miscalculated the baud
 * rate divisor entirely (wrong reference clock). CONFIRMED from
 * board/clock_config.c: UART_CLK_ROOT.outFreq = 80MHz, a single shared
 * clock root (kCLOCK_UartMux/kCLOCK_UartDiv) for ALL LPUART instances,
 * not per-instance -- same clock LPUART1/LPUART5 use. */
#define UHF_LPUART_CLK_FREQ_HZ 80000000UL

#define UHF_RX_RING_SIZE 2048u /* generously larger than the original's
                                   1023-byte EINBUFSIZE, since tag-heavy
                                   traffic can burst */

typedef struct {
    LPUART_Type *base;
    volatile uint8_t rx_ring[UHF_RX_RING_SIZE];
    volatile size_t rx_head;
    volatile size_t rx_tail;
} rt1062_uhf_ctx_t;

static rt1062_uhf_ctx_t s_ctx;

/* -------------------------------------------------------------------
 * ISR: pulls bytes out of the LPUART hardware FIFO into our ring
 * buffer. Kept minimal (no parsing here) -- matches the ISR-does-
 * almost-nothing discipline used elsewhere in this port (see
 * ds3231_rt1062.c).
 *
 * FOUND AND FIXED 2026-07-17, real root cause of "reading stalls after
 * ~10 seconds, board otherwise alive" -- this ISR only ever checked/
 * cleared kLPUART_RxDataRegFullFlag (RDRF), never
 * kLPUART_RxOverrunFlag (OR, LPUART_STAT_OR_MASK). Per the SDK's own
 * documented hardware behavior (fsl_lpuart.h), once OR sets ("new data
 * is received before data is read from receive register"), the
 * receiver stops transferring further data into the readable register
 * -- meaning RDRF never asserts again until OR is explicitly cleared.
 * Since this ISR was only ever entered via the RDRF interrupt (RIE),
 * and RDRF can't assert while OR is held, an overrun was a permanent,
 * self-sustaining deadlock: nothing left to ever re-enter this ISR and
 * clear OR. A bigger nand_log flush (more flash program/erase time,
 * PRIMASK disabled the whole time -- see mflash_drv.c) makes an
 * overrun far more likely, since LPUART8's hardware receive register
 * is only 1 byte deep and reader traffic keeps arriving the whole
 * time interrupts are off -- matches the observed correlation between
 * larger flushes and the stall exactly.
 *
 * Fixed two-part, both required together: (1) this ISR now also
 * checks for and clears kLPUART_RxOverrunFlag before draining RDRF --
 * the byte(s) that caused the overrun are unavoidably lost, but that's
 * the correct trade-off against permanently losing ALL future
 * reception; (2) rt1062_uhf_open() (below) now also enables
 * kLPUART_RxOverrunInterruptEnable (ORIE), not just RDRF's RIE --
 * without ORIE, an overrun with no accompanying new-RDRF-worthy byte
 * would never even re-enter this ISR in the first place, since RDRF
 * (the only previously-enabled interrupt source) is exactly what OR
 * suppresses. */
void LPUART8_IRQHandler(void)
{
    if (LPUART_GetStatusFlags(UHF_LPUART_BASE) & kLPUART_RxOverrunFlag) {
        LPUART_ClearStatusFlags(UHF_LPUART_BASE, kLPUART_RxOverrunFlag);
    }

    while (LPUART_GetStatusFlags(UHF_LPUART_BASE) & kLPUART_RxDataRegFullFlag) {
        uint8_t byte = LPUART_ReadByte(UHF_LPUART_BASE);
        size_t next_head = (s_ctx.rx_head + 1) % UHF_RX_RING_SIZE;
        if (next_head != s_ctx.rx_tail) { /* drop byte silently on overrun rather than corrupt the ring */
            s_ctx.rx_ring[s_ctx.rx_head] = byte;
            s_ctx.rx_head = next_head;
        }
    }
    SDK_ISR_EXIT_BARRIER;
}

/* ------------------------------------------------------------------ */

static int rt1062_uhf_open(void *ctx, uint32_t baud_rate)
{
    rt1062_uhf_ctx_t *c = (rt1062_uhf_ctx_t *)ctx;
    lpuart_config_t config;

    /* TODO: pin muxing for LPUART8 TX/RX must happen in pin_mux.c
     * before this runs -- not duplicated here, same reasoning as the
     * other hardware scaffolds in this port. */

    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = baud_rate;
    config.enableTx = true;
    config.enableRx = true;

    if (LPUART_Init(UHF_LPUART_BASE, &config, UHF_LPUART_CLK_FREQ_HZ) != kStatus_Success) {
        return -1;
    }

    c->base = UHF_LPUART_BASE;
    c->rx_head = 0;
    c->rx_tail = 0;

    /* kLPUART_RxOverrunInterruptEnable added 2026-07-17 alongside the
     * ISR fix above -- without it, an overrun with no accompanying new
     * RDRF-worthy byte would never re-enter LPUART8_IRQHandler() at
     * all, since RDRF (the only previously-enabled source) is exactly
     * what an unaddressed overrun suppresses. See that ISR's own
     * comment for the full story. */
    LPUART_EnableInterrupts(UHF_LPUART_BASE,
                             kLPUART_RxDataRegFullInterruptEnable | kLPUART_RxOverrunInterruptEnable);
    EnableIRQ(UHF_LPUART_IRQn);

    return 0;
}

static void rt1062_uhf_close(void *ctx)
{
    rt1062_uhf_ctx_t *c = (rt1062_uhf_ctx_t *)ctx;
    /* Was serEclose() -- referenced in the GENIE_SYSTEM touch-event
     * handler's UHF-off branch. Same shape as
     * gprs_transport_rt1062.c's rt1062_gprs_close(). Rabbit's serEclose
     * is safe to call on a port that was never opened (matches the
     * original calling it unconditionally on every switch away from
     * UHF, regardless of whether Open_Reader() ever actually ran this
     * session); LPUART_Deinit() is not -- a NULL/never-set base maps to
     * kCLOCK_IpInvalid internally and hits an index<=7 assert in
     * CLOCK_ControlGate(). Guard needed here, not in the caller, since
     * this is the one place that knows whether open() ever ran. */
    if (c->base == NULL) {
        return;
    }
    DisableIRQ(UHF_LPUART_IRQn);
    LPUART_Deinit(c->base);
    c->base = NULL;
}

static int rt1062_uhf_write(void *ctx, const uint8_t *buf, size_t len)
{
    rt1062_uhf_ctx_t *c = (rt1062_uhf_ctx_t *)ctx;
    status_t st = LPUART_WriteBlocking(c->base, buf, len);
    return (st == kStatus_Success) ? (int)len : -1;
}

static int rt1062_uhf_read(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    rt1062_uhf_ctx_t *c = (rt1062_uhf_ctx_t *)ctx;
    size_t n = 0;
    uint32_t elapsed_ms = 0;
    const uint32_t poll_interval_ms = 1;

    /* Polls the ring buffer filled by the ISR. Returns as soon as at
     * least one byte is available, then drains whatever's currently
     * in the ring, OR returns 0 if timeout_ms elapses with nothing
     * received at all.
     *
     * NOTE ON FIDELITY: the original's serEread(buf,size,timeout)
     * semantics for what exactly "timeout" measured (time to first
     * byte? total read window? inter-byte gap?) weren't fully
     * recoverable from the source alone. This implementation waits up
     * to timeout_ms for the FIRST byte, then drains whatever's
     * available without an additional inter-byte timeout. If your
     * reader's replies arrive in multiple bursts with gaps, you may
     * need to tune this -- worth verifying against actual captured
     * reader traffic. */
    while (elapsed_ms < timeout_ms) {
        if (c->rx_tail != c->rx_head) {
            break; /* at least one byte available */
        }
        /* FIXED (2026-07-13): was a placeholder loop shape with no actual
         * delay -- elapsed_ms incremented via pure register compare, so a
         * "timeout_ms" wait completed in low microseconds on real
         * hardware instead of the intended milliseconds, giving the
         * reader no realistic chance to reply. Same bug found and fixed
         * in gprs_transport_rt1062.c's rt1062_gprs_read() via a scope
         * trace showing real TX/RX activity the driver never captured. */
        SDK_DelayAtLeastUs(poll_interval_ms * 1000u, SystemCoreClock);
        elapsed_ms += poll_interval_ms;
    }

    while (n < max_len && c->rx_tail != c->rx_head) {
        buf[n] = c->rx_ring[c->rx_tail];
        c->rx_tail = (c->rx_tail + 1) % UHF_RX_RING_SIZE;
        n++;
    }

    return (int)n;
}

static void rt1062_uhf_flush_rx(void *ctx)
{
    rt1062_uhf_ctx_t *c = (rt1062_uhf_ctx_t *)ctx;
    c->rx_tail = c->rx_head; /* discard everything currently buffered */
}

static void rt1062_uhf_flush_tx(void *ctx)
{
    rt1062_uhf_ctx_t *c = (rt1062_uhf_ctx_t *)ctx;
    while (!(LPUART_GetStatusFlags(c->base) & kLPUART_TxDataRegEmptyFlag)) {
        /* wait for in-flight transmit to drain */
    }
}

static void rt1062_uhf_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    /* Was passing UHF_LPUART_CLK_FREQ_HZ (the peripheral's clock) here --
     * SDK_DelayAtLeastUs's second argument should be the CORE clock
     * (SystemCoreClock), same mistake already fixed in
     * gprs_transport_rt1062.c and neo_m8t_transport_rt1062.c. */
    SDK_DelayAtLeastUs(ms * 1000u, SystemCoreClock);
}

uhf_transport_t uhf_transport_rt1062_init(void)
{
    uhf_transport_t t;
    t.ctx = &s_ctx;
    t.open = rt1062_uhf_open;
    t.close = rt1062_uhf_close;
    t.write = rt1062_uhf_write;
    t.read = rt1062_uhf_read;
    t.flush_rx = rt1062_uhf_flush_rx;
    t.flush_tx = rt1062_uhf_flush_tx;
    t.delay_ms = rt1062_uhf_delay_ms;
    return t;
}
