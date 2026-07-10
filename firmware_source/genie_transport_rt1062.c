/*
 * genie_transport_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- LPUART instance now CONFIRMED (LPUART2). Not compiled/tested
 * against real hardware here, same caveats as the other *_rt1062.c files
 * in this port.
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
#define GENIE_LPUART_CLK_FREQ_HZ CLOCK_GetFreq(kCLOCK_Usb1PllClk)

#define GENIE_RX_RING_SIZE 256u

typedef struct {
    LPUART_Type *base;
    volatile uint8_t rx_ring[GENIE_RX_RING_SIZE];
    volatile size_t rx_head;
    volatile size_t rx_tail;
} rt1062_genie_ctx_t;

static rt1062_genie_ctx_t s_ctx;

void LPUART2_IRQHandler(void)
{
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

    LPUART_EnableInterrupts(GENIE_LPUART_BASE, kLPUART_RxDataRegFullInterruptEnable);
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
