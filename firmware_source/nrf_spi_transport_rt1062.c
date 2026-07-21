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
 * shared bus, corrected 2026-07-13 from an unverified Config Tools
 * default of 500kHz/Mode 0 to 1MHz/Mode 2, per explicit confirmation
 * against the original Dynamic C NEOM8T.LIB -- this bus is shared with
 * the GPS, so the same setting applies to both devices).
 *
 * SPI MODE EXPERIMENT, TRIED AND REVERTED 2026-07-13: the nRF52833
 * firmware's own SPIS config runs NRF_SPIS_MODE_1 (CPOL=0, CPHA=1), a
 * genuine mismatch against the bus's Mode 2 (CPOL=1, CPHA=0). Added a
 * per-transfer mode switch (disable LPSPI, rewrite TCR's CPOL/CPHA,
 * re-enable) here and the mirror-image switch in
 * neo_m8t_transport_rt1062.c. Result: fw_version changed from a
 * scrambled 0xDD to a clean 0xBB -- which is exactly the nRF SPIS
 * config's `.def` byte (the peripheral's own "no real reply queued
 * yet" default), not real firmware-version data. So Mode 1 likely *is*
 * electrically correct (we went from scrambled garbage to a real,
 * recognizable SPIS-defined constant), but something else -- probably
 * a readiness/queuing sequencing issue, since SPIS requires the nRF app
 * to pre-arm a TX buffer before each transfer completes, and this is a
 * single immediate transfer with no ready-line wait (faithful to the
 * original comms_NRF() 0x0E case) -- is still wrong. Meanwhile this
 * change broke GPS. Reverted back to the single static bus-wide Mode 2
 * per instruction, to get GPS working again; the nRF fw_version mystery
 * is still open, see project memory for the next angle to try (probably
 * the SPIS buffer-arming/readiness sequencing, not mode).
 *
 * CURRENT STATE, 2026-07-18: the nRF fw_version mystery from above is
 * RESOLVED -- real root causes were wrong CS setup timing (200us,
 * scope-measured) and wrong clock speed (3.125MHz, scope-measured
 * against the real Rabbit reference), not mode. The bus's STATIC config
 * is now permanently Mode 0 (GPS's own native default, confirmed via
 * the u-blox datasheet and empirically -- GPS never reliably ACKs at
 * Mode 1 on this hardware). The nRF's Mode 1 requirement is handled
 * per-transfer, entirely within this file -- see rt1062_transfer()'s
 * own switch-in/switch-out calls just below. This supersedes the
 * "REVERTED" note above; that whole-bus-Mode-2 approach is no longer
 * how this works. */

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

/* PER-TRANSFER MODE SWITCH, added 2026-07-18. This bus's static config
 * (`LPSPI3_config`, board/peripherals.c) is now permanently Mode 0 --
 * the NEO-M8T GPS's own true native/default mode (confirmed both by the
 * u-blox datasheet, which documents Mode 0 as the SPI default, and
 * empirically: GPS only ever got real ACKs at Mode 0 all session). An
 * earlier attempt tried a ONE-TIME bus-wide switch to Mode 1 (send
 * GPS's own UBX-CFG-PRT first at Mode 0, then leave the whole bus at
 * Mode 1 for the rest of boot) -- that got the nRF working but GPS's
 * remaining messages still failed completely, proving GPS genuinely
 * cannot tolerate Mode 1 on this hardware (unlike the real Rabbit
 * reference, which apparently can). So Mode 1 is now the nRF's own,
 * fully self-contained, PER-TRANSFER concern: switch in immediately
 * before this transfer, switch back to Mode 0 (the bus's permanent
 * default) immediately after -- GPS (a separate transport entirely,
 * neo_m8t_transport_rt1062.c) never sees anything but Mode 0, at any
 * point, ever. Uses the exact TCR-register-level mechanism already
 * scope-verified working on 2026-07-14 for this same kind of runtime
 * mode change (disable peripheral, rewrite ONLY the CPOL/CPHA bits,
 * re-enable) -- confirmed bit positions against the real device header
 * (LPSPI_TCR_CPOL_MASK=bit31, LPSPI_TCR_CPHA_MASK=bit30), matching the
 * historically-confirmed TCR readback value from that investigation
 * (0x40000007 = CPHA set, CPOL clear, FRAMESZ=7 -- i.e. Mode 1). */
static void rt1062_switch_to_mode1(LPSPI_Type *base)
{
    LPSPI_Enable(base, false);
    base->TCR = (base->TCR & ~(LPSPI_TCR_CPOL_MASK | LPSPI_TCR_CPHA_MASK))
                | LPSPI_TCR_CPOL(0U) | LPSPI_TCR_CPHA(1U);
    LPSPI_Enable(base, true);
}

static void rt1062_switch_to_mode0(LPSPI_Type *base)
{
    LPSPI_Enable(base, false);
    base->TCR = (base->TCR & ~(LPSPI_TCR_CPOL_MASK | LPSPI_TCR_CPHA_MASK))
                | LPSPI_TCR_CPOL(0U) | LPSPI_TCR_CPHA(0U);
    LPSPI_Enable(base, true);
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

    rt1062_switch_to_mode1(hw->base);
    status = LPSPI_MasterTransferBlocking(hw->base, &xfer);
    rt1062_switch_to_mode0(hw->base);

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
