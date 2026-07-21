/*
 * genie_transport_rt1062.c
 *
 * ==========================================================================
 * CONFIRMED WORKING ON REAL HARDWARE, 2026-07-14 (LPUART2, Genie 4D
 * display) -- built/flashed via the separate real MCUXpresso project
 * (eaimxrt1062_hello_world) this port develops alongside; splash string
 * written via genie_write_str() round-tripped correctly (display
 * detected, ACK received). As integrated into THIS repo's
 * app_init.c/app_loop.c specifically, still not rebuilt/retested here --
 * same scaffold-tier caveat as the other *_rt1062.c files, review the
 * real project's hello_world.c bring-up test if anything behaves
 * unexpectedly. Touch/event RX (display -> RT1062 direction) not yet
 * separately confirmed, only TX (RT1062 -> display) so far.
 * ==========================================================================
 *
 * Implements genie_transport_t using LPUART in interrupt-driven RX (ring
 * buffer) + blocking TX mode, same ISR-does-almost-nothing discipline as
 * uhf_transport_rt1062.c. Was open_4D()'s serCopen/serCputc/serCgetc/
 * serCpeek/serCrdUsed/serCrdFlush/serCwrFlush.
 */

#include "genie_transport_rt1062.h"

#include "fsl_lpuart.h"
#include "fsl_common.h"
#include "fsl_clock.h" /* explicit now -- was relying on a transitive include via fsl_lpi2c.h that only existed in this port's host-test stub, not necessarily your real SDK's header chain */
#include <string.h>

/* CONFIRMED: LPUART2, wired to the Genie display's serial line. All
 * four LPUART instances used by this port are now confirmed: LPUART1
 * (modem), LPUART2 (display), LPUART5 (FTDI, reserved, no code),
 * LPUART8 (UHF reader). */
#define GENIE_LPUART_BASE       LPUART2
#define GENIE_LPUART_IRQn       LPUART2_IRQn
/* FIXED (2026-07-14, before first hardware test): was
 * CLOCK_GetFreq(kCLOCK_Usb1PllClk), the same wrong-clock-root bug
 * already found and fixed in uhf_transport_rt1062.c's
 * UHF_LPUART_CLK_FREQ_HZ -- would have miscalculated the baud rate
 * divisor entirely. board/clock_config.c: UART_CLK_ROOT.outFreq = 80MHz,
 * a single shared root for every LPUART instance (1/2/5/8, confirmed
 * via clock_config.h's own consumer comment) -- not per-instance PLL
 * clocks. Caught by inspection before ever running on hardware, unlike
 * the UHF case which needed a real bring-up failure to surface it. */
#define GENIE_LPUART_CLK_FREQ_HZ 80000000UL

#define GENIE_RX_RING_SIZE 256u

typedef struct {
    LPUART_Type *base;
    volatile uint8_t rx_ring[GENIE_RX_RING_SIZE];
    volatile size_t rx_head;
    volatile size_t rx_tail;
} rt1062_genie_ctx_t;

static rt1062_genie_ctx_t s_ctx;

/* Overrun handling added 2026-07-17 -- see uhf_transport_rt1062.c's
 * LPUART8_IRQHandler() for the full story (found there first, this
 * driver shares the identical gap): without checking/clearing
 * kLPUART_RxOverrunFlag, an overrun permanently deadlocks RDRF (and
 * therefore this whole ISR) since RDRF can't assert again while OR is
 * held -- nothing left to ever re-enter this handler and clear it. A
 * particularly plausible culprit for any past "display stopped
 * responding" investigation in this project, given the Genie link's
 * own continuous ~1250ms auto-ping traffic. */
void LPUART2_IRQHandler(void)
{
    if (LPUART_GetStatusFlags(GENIE_LPUART_BASE) & kLPUART_RxOverrunFlag) {
        LPUART_ClearStatusFlags(GENIE_LPUART_BASE, kLPUART_RxOverrunFlag);
    }

    while (LPUART_GetStatusFlags(GENIE_LPUART_BASE) & kLPUART_RxDataRegFullFlag) {
        uint8_t byte = LPUART_ReadByte(GENIE_LPUART_BASE);
        size_t next_head = (s_ctx.rx_head + 1) % GENIE_RX_RING_SIZE;
        if (next_head != s_ctx.rx_tail) {
            s_ctx.rx_ring[s_ctx.rx_head] = byte;
            s_ctx.rx_head = next_head;
        }
    }
    SDK_ISR_EXIT_BARRIER;
}

static void rt1062_genie_write(void *ctx, const uint8_t *buf, size_t len)
{
    rt1062_genie_ctx_t *c = (rt1062_genie_ctx_t *)ctx;
    LPUART_WriteBlocking(c->base, buf, len);
}

static int rt1062_genie_read_available(void *ctx)
{
    rt1062_genie_ctx_t *c = (rt1062_genie_ctx_t *)ctx;
    uint32_t primask = DisableGlobalIRQ();
    size_t head = c->rx_head, tail = c->rx_tail;
    EnableGlobalIRQ(primask);
    return (int)((head + GENIE_RX_RING_SIZE - tail) % GENIE_RX_RING_SIZE);
}

static int rt1062_genie_peek(void *ctx)
{
    rt1062_genie_ctx_t *c = (rt1062_genie_ctx_t *)ctx;
    if (rt1062_genie_read_available(ctx) <= 0) {
        return -1;
    }
    return c->rx_ring[c->rx_tail];
}

static int rt1062_genie_getc(void *ctx)
{
    rt1062_genie_ctx_t *c = (rt1062_genie_ctx_t *)ctx;
    uint8_t byte = c->rx_ring[c->rx_tail];
    c->rx_tail = (c->rx_tail + 1) % GENIE_RX_RING_SIZE;
    return byte;
}

static void rt1062_genie_flush_rx(void *ctx)
{
    rt1062_genie_ctx_t *c = (rt1062_genie_ctx_t *)ctx;
    uint32_t primask = DisableGlobalIRQ();
    c->rx_tail = c->rx_head;
    EnableGlobalIRQ(primask);
}

static void rt1062_genie_flush_tx(void *ctx)
{
    rt1062_genie_ctx_t *c = (rt1062_genie_ctx_t *)ctx;
    while (!(LPUART_GetStatusFlags(c->base) & kLPUART_TxDataRegEmptyFlag)) {
        /* wait */
    }
}

genie_transport_t genie_transport_rt1062_init(void)
{
    lpuart_config_t config;
    genie_transport_t t;

    memset(&s_ctx, 0, sizeof(s_ctx));

    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = GENIE_UART_BAUDRATE;
    config.enableTx = true;
    config.enableRx = true;
    LPUART_Init(GENIE_LPUART_BASE, &config, GENIE_LPUART_CLK_FREQ_HZ);

    s_ctx.base = GENIE_LPUART_BASE;

    /* kLPUART_RxOverrunInterruptEnable added 2026-07-17 alongside the
     * ISR fix above -- without it, an overrun could never re-trigger
     * this ISR to clear itself (RDRF, the only previously-enabled
     * source, is exactly what an unaddressed overrun suppresses). */
    LPUART_EnableInterrupts(GENIE_LPUART_BASE,
                             kLPUART_RxDataRegFullInterruptEnable | kLPUART_RxOverrunInterruptEnable);
    EnableIRQ(GENIE_LPUART_IRQn);

    t.ctx = &s_ctx;
    t.write = rt1062_genie_write;
    t.read_available = rt1062_genie_read_available;
    t.peek = rt1062_genie_peek;
    t.getc = rt1062_genie_getc;
    t.flush_rx = rt1062_genie_flush_rx;
    t.flush_tx = rt1062_genie_flush_tx;
    return t;
}
