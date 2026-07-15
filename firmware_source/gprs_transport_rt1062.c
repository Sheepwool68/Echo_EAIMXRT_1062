/*
 * gprs_transport_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- LPUART instance/clock and the wake pin are now CONFIRMED from
 * peripherals.h/peripherals.c (MCUXpresso Config Tools generated). Only the
 * power-enable pin (was PA2) remains an unconfirmed placeholder.
 * ==========================================================================
 *
 * Implements gprs_transport_t using LPUART (interrupt-driven RX ring
 * buffer, same shape as uhf_transport_rt1062.c) plus two GPIO outputs.
 * The wake pin now goes through the HAL GPIO adapter (GPIO1_MODEM_DTR_handle,
 * from peripherals.h) instead of the raw fsl_gpio.h driver, since that's
 * what your generated peripherals.c actually sets up for this pin.
 */

#include "gprs_transport_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated --
    GPIO1_MODEM_DTR_handle, LPUART1_PERIPHERAL, LPUART1_CLOCK_SOURCE */

#include "fsl_lpuart.h"
#include "fsl_common.h"
#include "fsl_clock.h" /* explicit now -- was relying on a transitive include via fsl_lpi2c.h that only existed in this port's host-test stub, not necessarily your real SDK's header chain */
#include <string.h>

/* CONFIRMED from peripherals.h: LPUART1, 80MHz source clock. Was
 * LPUART5 (itself a fix for an earlier placeholder collision with the
 * display's LPUART4) -- both were placeholders; this is the real
 * instance. CONFIRMED: LPUART5 is real hardware wired to an FTDI
 * USB-serial chip (reserved for future use, no code yet) -- NOT free
 * for this port to assign elsewhere. All four LPUART instances used
 * by this port are now confirmed: LPUART1 (modem, this file),
 * LPUART2 (display), LPUART5 (FTDI, reserved), LPUART8 (UHF reader). */
#define GPRS_LPUART_BASE        LPUART1_PERIPHERAL
#define GPRS_LPUART_IRQn        LPUART1_IRQn
#define GPRS_LPUART_CLK_FREQ_HZ LPUART1_CLOCK_SOURCE

/*
 * CONFIRMED from peripherals.h/.c: GPIO1 pin 24 (was PE2's "DTR2" wake
 * line in program_init(), "PE2 LO - wakeup" / "PE2 HI - allow sleep").
 * Goes through the HAL GPIO adapter now (GPIO1_MODEM_DTR_handle),
 * not GPIO_PinInit/GPIO_PinWrite -- BOARD_InitPeripherals() already
 * calls HAL_GpioInit() for this pin, so this file must NOT also call
 * GPIO_PinInit() on it (that would be the old raw-driver approach
 * fighting the HAL adapter's own state).
 *
 * *** REAL CONFLICT, NOT YET RESOLVED -- flagging rather than picking
 * one silently ***: program_init() drives this pin LOW at boot
 * ("wakeup"). peripherals.c's generated default initializes it HIGH
 * (GPIO1_MODEM_DTR_PIN_LEVEL 1U). Those are opposite states for the
 * same signal. This file's rt1062_gprs_set_wake_pin() below correctly
 * maps level=0 to "wake" / level=1 to "sleep" either way (matching the
 * original's documented meaning) -- what's UNRESOLVED is which state
 * the modem should actually be in at boot, before any explicit
 * set_wake_pin() call. If you want wake-at-boot (matching
 * program_init()), change the initial level via MCUXpresso Config
 * Tools (not by hand-editing peripherals.c, which the tool will
 * overwrite) rather than in this file.
 *
 * NOT CONFIRMED: the actual GPIO output-write function
 * (HAL_GpioSetOutput below) -- peripherals.c only showed HAL_GpioInit/
 * InstallCallback/SetTriggerMode for pins with callbacks; MODEM_DTR
 * has none, so no write-call example exists in what was pasted. This
 * name/signature is a best-effort guess based on this adapter's
 * typical shape, not confirmed against your actual fsl_adapter_gpio.h.
 */
#define GPRS_WAKE_HANDLE         GPIO1_MODEM_DTR_handle

/*
 * CONFIRMED from peripherals.h: gpio_io.22, "MODEM_PWR" in the
 * generated peripherals, output, initial level 0 -- matches
 * program_init()'s `BitWrPortI(PADR, &PADRShadow, 0, 2); // EN power
 * off Modem` exactly (was PA2, initial state "power off"). Goes
 * through the HAL adapter (BOARD_INITPINS_MODEM_PWR_handle), same as
 * the wake pin above.
 */
#define GPRS_POWER_EN_HANDLE     BOARD_INITPINS_MODEM_PWR_handle

#define GPRS_RX_RING_SIZE 512u /* original's FINBUFSIZE was 127; sized up
                                   for headroom on config/GPRS record traffic */

typedef struct {
    LPUART_Type *base;
    volatile uint8_t rx_ring[GPRS_RX_RING_SIZE];
    volatile size_t rx_head;
    volatile size_t rx_tail;
} rt1062_gprs_ctx_t;

static rt1062_gprs_ctx_t s_ctx;

void LPUART1_IRQHandler(void)
{
    while (LPUART_GetStatusFlags(GPRS_LPUART_BASE) & kLPUART_RxDataRegFullFlag) {
        uint8_t byte = LPUART_ReadByte(GPRS_LPUART_BASE);
        size_t next_head = (s_ctx.rx_head + 1) % GPRS_RX_RING_SIZE;
        if (next_head != s_ctx.rx_tail) {
            s_ctx.rx_ring[s_ctx.rx_head] = byte;
            s_ctx.rx_head = next_head;
        }
    }
    SDK_ISR_EXIT_BARRIER;
}

static int rt1062_gprs_open(void *ctx, uint32_t baud_rate)
{
    rt1062_gprs_ctx_t *c = (rt1062_gprs_ctx_t *)ctx;
    lpuart_config_t config;

    /* TODO: pin muxing for LPUART1 TX/RX and GPRS_POWER_EN_GPIO must
     * happen in pin_mux.c before this runs. GPIO1_MODEM_DTR's own pin
     * muxing is already handled by BOARD_InitPeripherals(). */

    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = baud_rate;
    config.enableTx = true;
    config.enableRx = true;

    if (LPUART_Init(GPRS_LPUART_BASE, &config, GPRS_LPUART_CLK_FREQ_HZ) != kStatus_Success) {
        return -1;
    }

    c->base = GPRS_LPUART_BASE;
    c->rx_head = 0;
    c->rx_tail = 0;

    LPUART_EnableInterrupts(GPRS_LPUART_BASE, kLPUART_RxDataRegFullInterruptEnable);
    EnableIRQ(GPRS_LPUART_IRQn);

    return 0;
}

static void rt1062_gprs_close(void *ctx)
{
    rt1062_gprs_ctx_t *c = (rt1062_gprs_ctx_t *)ctx;
    DisableIRQ(GPRS_LPUART_IRQn);
    LPUART_Deinit(c->base);
}

static int rt1062_gprs_write(void *ctx, const uint8_t *buf, size_t len)
{
    rt1062_gprs_ctx_t *c = (rt1062_gprs_ctx_t *)ctx;
    status_t st = LPUART_WriteBlocking(c->base, buf, len);
    return (st == kStatus_Success) ? (int)len : -1;
}

static int rt1062_gprs_read(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms)
{
    rt1062_gprs_ctx_t *c = (rt1062_gprs_ctx_t *)ctx;
    size_t n = 0;
    uint32_t elapsed_ms = 0;
    const uint32_t poll_interval_ms = 1;

    /* BUG FIX (2026-07-13): this loop incremented elapsed_ms without ever
     * actually waiting -- a pure register increment/compare loop with no
     * delay completes a few-hundred-iteration timeout in low
     * microseconds on real hardware, not the intended milliseconds. The
     * modem never had a realistic chance to reply before this returned.
     * Confirmed on real hardware: scope showed genuine TX/RX activity on
     * LPUART1, but this function always reported zero bytes. */
    while (elapsed_ms < timeout_ms) {
        if (c->rx_tail != c->rx_head) {
            break;
        }
        SDK_DelayAtLeastUs(poll_interval_ms * 1000u, SystemCoreClock);
        elapsed_ms += poll_interval_ms;
    }

    while (n < max_len && c->rx_tail != c->rx_head) {
        buf[n] = c->rx_ring[c->rx_tail];
        c->rx_tail = (c->rx_tail + 1) % GPRS_RX_RING_SIZE;
        n++;
    }

    return (int)n;
}

static void rt1062_gprs_flush_rx(void *ctx)
{
    rt1062_gprs_ctx_t *c = (rt1062_gprs_ctx_t *)ctx;
    c->rx_tail = c->rx_head;
}

static void rt1062_gprs_flush_tx(void *ctx)
{
    rt1062_gprs_ctx_t *c = (rt1062_gprs_ctx_t *)ctx;
    while (!(LPUART_GetStatusFlags(c->base) & kLPUART_TxDataRegEmptyFlag)) {
        /* wait for in-flight transmit to drain */
    }
}

static void rt1062_gprs_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    /* Was passing GPRS_LPUART_CLK_FREQ_HZ (the peripheral's clock)
     * here -- SDK_DelayAtLeastUs's second argument should be the CORE
     * clock (SystemCoreClock), same mistake flagged in
     * neo_m8t_transport_rt1062.c's rt1062_delay_ms(). Fixed here too. */
    SDK_DelayAtLeastUs(ms * 1000u, SystemCoreClock);
}

static void rt1062_gprs_set_wake_pin(void *ctx, int level)
{
    (void)ctx;
    /* Was GPIO_PinWrite(GPRS_WAKE_GPIO, GPRS_WAKE_PIN, ...) -- now via
     * the HAL adapter handle BOARD_InitPeripherals() already set up.
     * level=0 -> wake (was PE2 LO), level=1 -> allow sleep (was PE2 HI),
     * matching program_init()'s documented meaning exactly. See this
     * file's header comment on HAL_GpioSetOutput's name/signature not
     * being confirmed from any pasted source. */
    HAL_GpioSetOutput(GPRS_WAKE_HANDLE, (uint8_t)level);
}

static void rt1062_gprs_set_power_enable(void *ctx, int level)
{
    (void)ctx;
    /* Was GPIO_PinWrite(GPRS_POWER_EN_GPIO, GPRS_POWER_EN_PIN, ...) --
     * now via the HAL adapter handle BOARD_InitPeripherals() already
     * set up. */
    HAL_GpioSetOutput(GPRS_POWER_EN_HANDLE, (uint8_t)level);
}

gprs_transport_t gprs_transport_rt1062_init(void)
{
    gprs_transport_t t;

    /* Neither GPIO pin (wake / GPIO1_MODEM_DTR, power-enable /
     * BOARD_INITPINS_MODEM_PWR) is initialized here anymore -- both
     * are already handled by BOARD_InitPeripherals() via the HAL
     * adapter. Calling GPIO_PinInit() on either here too (the old
     * raw-driver approach) would fight the HAL adapter's own state.
     * Make sure BOARD_InitPeripherals() runs before this function. */

    t.ctx = &s_ctx;
    t.open = rt1062_gprs_open;
    t.close = rt1062_gprs_close;
    t.write = rt1062_gprs_write;
    t.read = rt1062_gprs_read;
    t.flush_rx = rt1062_gprs_flush_rx;
    t.flush_tx = rt1062_gprs_flush_tx;
    t.delay_ms = rt1062_gprs_delay_ms;
    t.set_wake_pin = rt1062_gprs_set_wake_pin;
    t.set_power_enable = rt1062_gprs_set_power_enable;
    return t;
}
