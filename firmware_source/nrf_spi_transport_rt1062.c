/*
 * nrf_spi_transport_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- LPSPI instance, mode, and baud are CONFIRMED from
 * peripherals.h/peripherals.c (MCUXpresso Config Tools generated). CS is
 * back to manual GPIO per your instruction ("the LPSPI driver only handles
 * one CS pin") -- see below. Not compiled/tested against real hardware
 * here.
 * ==========================================================================
 *
 * Implements the nrf_spi_transport_t vtable (see nrf_spi_transport.h)
 * using LPSPI3 in blocking/polling mode, sharing the bus with the GPS
 * NEO-M8T (see neo_m8t_transport_rt1062.c) -- same MOSI/MISO/CLK lines.
 *
 * ARCHITECTURAL HISTORY on CS handling, since this has changed twice:
 * originally manual GPIO (matching the original Dynamic C source, which
 * bit-banged CS for the same reason described below); briefly switched to
 * hardware PCS0/PCS1 per-transfer once peripherals.c showed
 * pcsFunc: kLPSPI_PcsAsCs; now REVERTED back to manual GPIO per your
 * explicit instruction, since the LPSPI driver only manages one hardware
 * CS pin, not two independent ones for a shared bus with two devices.
 * This still reproduces the original's exact behavior: every call site in
 * nrf_spi_protocol.c follows a strict cs_assert()-transfer()-cs_deassert()
 * triple with no cases of multiple transfers inside one assert/deassert
 * window (confirmed by inspection), so manual GPIO toggling around each
 * transfer() call reproduces the "drop and re-raise between phases"
 * behavior exactly, same as it always did before the brief PCS detour.
 *
 * BOARD_InitPeripherals() still calls LPSPI_MasterInit() for LPSPI3 (a
 * shared bus) -- this file must NOT also call it, and must NOT reconfigure
 * the bus's global settings (baud/mode are fixed for both devices on this
 * shared bus, confirmed 500kHz / SPI Mode 0). Only CS is manual now, not
 * the bus itself.
 */

#include "nrf_spi_transport_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated --
    LPSPI3_PERIPHERAL */

#include "fsl_lpspi.h"
#include "fsl_gpio.h"
#include "fsl_common.h"
#include "fsl_clock.h" /* explicit now -- was relying on a transitive include via fsl_lpi2c.h that only existed in this port's host-test stub, not necessarily your real SDK's header chain */

/* CONFIRMED from peripherals.c: LPSPI3, shared with the GPS NEO-M8T. */
#define NRF_LPSPI_BASE          LPSPI3_PERIPHERAL

/* CONFIRMED: GPIO_AD_B1_12 = GPIO1 pin 28 (standard RT1062 pad-naming
 * convention -- GPIO_AD_B1_xx continues the same GPIO1 instance past
 * the GPIO_AD_B0 bank, so AD_B1_12 = GPIO1[16+12] = pin 28), now used
 * as a plain GPIO output for manual CS control (see header comment on
 * the hardware-PCS0 approach being reverted). Verify this pad's mux is
 * actually set to plain GPIO in the Pins tool, not left as
 * LPSPI3_PCS0 (which is what it was set to when hardware PCS was
 * still the plan). */
#define NRF_CS_GPIO             GPIO1
#define NRF_CS_PIN              28u

/* CONFIRMED from pin_mux.h: GPIO_SD_B1_02 = GPIO3 pin 2 -- actual
 * ready/attention input pin (was PBDR bit 3 on the Rabbit). Not part
 * of the SPI bus itself. */
#define NRF_READY_GPIO          GPIO3
#define NRF_READY_PIN           2u

/* -------------------------------------------------------------------
 * Static state (single instance; extend to a context struct if you
 * ever need more than one nRF52833 link).
 * ---------------------------------------------------------------- */
typedef struct {
    LPSPI_Type *base;
    int initialized;
} rt1062_nrf_hw_ctx_t;

static rt1062_nrf_hw_ctx_t s_hw_ctx;

/* -------------------------------------------------------------------
 * Transport vtable implementation
 * ---------------------------------------------------------------- */

static void rt1062_cs_assert(void *ctx)
{
    (void)ctx;
    GPIO_PinWrite(NRF_CS_GPIO, NRF_CS_PIN, 0u); /* active low, matches original CS1_ENABLE writing 0 */
}

static void rt1062_cs_deassert(void *ctx)
{
    (void)ctx;
    GPIO_PinWrite(NRF_CS_GPIO, NRF_CS_PIN, 1u);
}

static int rt1062_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    rt1062_nrf_hw_ctx_t *hw = (rt1062_nrf_hw_ctx_t *)ctx;
    lpspi_transfer_t xfer = {0};
    status_t status;

    xfer.txData = (uint8_t *)tx;   /* SDK API takes non-const; safe, driver doesn't mutate tx */
    xfer.rxData = rx;
    xfer.dataSize = len;
    /* Was kLPSPI_MasterPcs0 (brief hardware-PCS detour). Reverted to
     * manual CS control (see file header) -- using continuous transfer
     * with no PCS-selection flag needed, since CS is handled entirely
     * outside the LPSPI peripheral now via cs_assert()/cs_deassert(). */
    xfer.configFlags = kLPSPI_MasterPcsContinuous;

    status = LPSPI_MasterTransferBlocking(hw->base, &xfer);
    return (status == kStatus_Success) ? 0 : -1;
}

static int rt1062_wait_ready_line(void *ctx, int level, uint32_t timeout_ms)
{
    (void)ctx;
    uint32_t elapsed_ms = 0;
    const uint32_t poll_interval_us = 100;

    while (elapsed_ms < timeout_ms) {
        int current = (int)GPIO_PinRead(NRF_READY_GPIO, NRF_READY_PIN);
        if (current == level) {
            return 0;
        }
        SDK_DelayAtLeastUs(poll_interval_us, SystemCoreClock);
        elapsed_ms += (poll_interval_us >= 1000) ? (poll_interval_us / 1000) : 1;
    }
    return -1;
}

static int rt1062_read_ready_line(void *ctx)
{
    (void)ctx;
    return (int)GPIO_PinRead(NRF_READY_GPIO, NRF_READY_PIN);
}

static void rt1062_delay_us(void *ctx, uint32_t us)
{
    (void)ctx;
    SDK_DelayAtLeastUs(us, SystemCoreClock);
}

static void rt1062_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    SDK_DelayAtLeastUs(ms * 1000u, SystemCoreClock);
}

/* -------------------------------------------------------------------
 * Public init: call once at startup, then pass the returned transport
 * to the nrf_spi_protocol.h functions.
 * ---------------------------------------------------------------- */

nrf_spi_transport_t nrf_spi_transport_rt1062_init(void)
{
    gpio_pin_config_t cs_config = { kGPIO_DigitalOutput, 1, kGPIO_NoIntmode };
    gpio_pin_config_t ready_config = { kGPIO_DigitalInput, 0, kGPIO_NoIntmode };
    nrf_spi_transport_t t;

    /* LPSPI3 bus init (instance, baud, mode) is NOT done here -- 
     * BOARD_InitPeripherals() already does it via LPSPI3_init(), since
     * this is a shared bus with the GPS NEO-M8T. Calling
     * LPSPI_MasterInit() here too would reconfigure a bus another
     * device also depends on. CS, however, IS this file's
     * responsibility again (manual GPIO, not the peripheral's PCS) --
     * see file header.
     *
     * Pin muxing for NRF_CS_GPIO/PIN and NRF_READY_GPIO/PIN is CONFIRMED
     * handled by pin_mux.c's BOARD_InitPins() (MCUXpresso Config
     * Tools generated) -- make sure that runs before this function. */

    GPIO_PinInit(NRF_CS_GPIO, NRF_CS_PIN, &cs_config);
    GPIO_PinWrite(NRF_CS_GPIO, NRF_CS_PIN, 1u); /* idle high (deasserted) */
    GPIO_PinInit(NRF_READY_GPIO, NRF_READY_PIN, &ready_config);

    s_hw_ctx.base = NRF_LPSPI_BASE;
    s_hw_ctx.initialized = 1;

    t.hw_ctx = &s_hw_ctx;
    t.cs_assert = rt1062_cs_assert;
    t.cs_deassert = rt1062_cs_deassert;
    t.transfer = rt1062_transfer;
    t.wait_ready_line = rt1062_wait_ready_line;
    t.read_ready_line = rt1062_read_ready_line;
    t.delay_us = rt1062_delay_us;
    t.delay_ms = rt1062_delay_ms;

    return t;
}
