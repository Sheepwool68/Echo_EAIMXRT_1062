/*
 * neo_m8t_transport_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- LPSPI instance, mode, baud, and CS pin are CONFIRMED. Not
 * compiled/tested against real hardware here.
 * ==========================================================================
 *
 * Implements neo_m8t_transport_t using LPSPI3 in blocking mode, sharing
 * the bus with the nRF52833 (see nrf_spi_transport_rt1062.c) -- same
 * MOSI/MISO/CLK lines, separate manual GPIO chip-selects (see below), plus
 * the PPS GPIO interrupt (was my_isr()).
 *
 * ARCHITECTURAL HISTORY on CS handling, same as nrf_spi_transport_rt1062.c:
 * originally manual GPIO; briefly switched to hardware PCS1 once
 * peripherals.c showed pcsFunc: kLPSPI_PcsAsCs and the pad was muxed to
 * LPSPI3_PCS1 (GPIO_AD_B0_04, confirmed via the Pins tool); now REVERTED
 * back to manual GPIO per your explicit instruction, since the LPSPI
 * driver only manages one hardware CS pin, not two independent ones for a
 * shared bus with two devices. This still reproduces the original's exact
 * behavior: every call site in neo_m8t_reader.c follows a strict
 * cs_enable()-transfer()-cs_disable() triple with exactly ONE transfer()
 * call per window (confirmed by inspection), so manual GPIO toggling
 * around each transfer() call reproduces this exactly, same as it always
 * did before the brief PCS detour.
 *
 * CS pin: GPIO_AD_B0_04, which by the RT1062's standard pad-naming
 * convention (GPIO_AD_B0_xx pads default to GPIO1 pin xx) is GPIO1 pin 4.
 * Now reconfigured as a plain GPIO output instead of the LPSPI3_PCS1 ALT
 * function -- verify this pad's mux is actually set back to GPIO in the
 * Pins tool (not left as LPSPI3_PCS1) since that ALT-function assignment
 * was made when the hardware-PCS approach was still the plan.
 *
 * BOARD_InitPeripherals() still calls LPSPI_MasterInit() for LPSPI3 (a
 * shared bus) -- this file must NOT also call it. Only CS is manual now,
 * not the bus itself.
 *
 * Per-transfer SPI mode switching (matching the nRF52833's own Mode 1)
 * was tried here and in nrf_spi_transport_rt1062.c on 2026-07-13, but it
 * broke GPS without actually fixing the nRF (fw_version came back as the
 * nRF SPIS peripheral's own `.def` default-byte constant, 0xBB, not real
 * data -- pointing at a readiness/buffer-queuing issue on the nRF side
 * instead). Reverted back to this file's single static Mode 2, per
 * instruction, to restore GPS. See nrf_spi_transport_rt1062.c's header
 * for the full finding.
 */

#include "neo_m8t_transport_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated -- declares
    PPS_Handler's expected signature, GPIO1_PPS_*, LPSPI3_PERIPHERAL */

#include "fsl_lpspi.h"
#include "fsl_gpio.h"
#include "fsl_common.h"
#include "fsl_clock.h" /* explicit now -- was relying on a transitive include via fsl_lpi2c.h that only existed in this port's host-test stub, not necessarily your real SDK's header chain */
#include <string.h>

/* CONFIRMED from peripherals.c: LPSPI3, shared with the nRF52833. */
#define NEO_LPSPI_BASE          LPSPI3_PERIPHERAL

/* CONFIRMED: GPIO_AD_B0_04 = GPIO1 pin 4 (standard RT1062 pad-naming
 * convention), now used as a plain GPIO output for manual CS control
 * (see header comment on the hardware-PCS1 approach being reverted). */
#define NEO_CS_GPIO             GPIO1
#define NEO_CS_PIN              4u

/* PPS pin definitions removed from here -- GPIO1_PPS_PORT/PIN and the
 * whole init/interrupt/callback setup now live in the MCUXpresso
 * Config Tools-generated peripherals.h/peripherals.c (GPIO1_PPS_handle
 * etc). This file only implements PPS_Handler() below. */

typedef struct {
    LPSPI_Type *base;
} rt1062_neo_hw_ctx_t;

static rt1062_neo_hw_ctx_t s_hw_ctx;

/* CONFIRMED from peripherals.h/peripherals.c (MCUXpresso Config
 * Tools-generated): GPIO1 pin 2, rising-edge trigger, callback name
 * PPS_Handler. Uses the HAL GPIO adapter (fsl_adapter_gpio.h,
 * GPIO1_PPS_handle/GPIO1_PPS_PORT/PIN from peripherals.h) rather than
 * the raw fsl_gpio.h driver this file used before -- that's a real
 * architectural change, not just a pin renumbering, and it actually
 * resolves the ds3231_rt1062.c-vs-this-file shared-combined-vector
 * collision flagged earlier: the HAL adapter dispatches callbacks
 * per-pin internally, so DS3231's SQW callback and this one no longer
 * need to share one hand-written IRQHandler function name. */

/* -------------------------------------------------------------------
 * PPS interrupt (was my_isr()). Kept minimal -- capture-and-flag only,
 * same discipline as before, just via the HAL adapter's callback
 * mechanism instead of a raw vector-table IRQHandler.
 *
 * iDSTimeFromRabbit-equivalent diagnostic (was time_diff_pps =
 * iGPSTimeFromRabbit - iDSTimeFromRabbit): see the prior version of
 * this comment -- still diagnostic-only, no active consumer.
 * ---------------------------------------------------------------- */
static volatile uint32_t s_gps_tick;
static volatile int s_pps_state;
static volatile uint32_t s_gps_time_from_rabbit_ms; /* diagnostic only, see above */

extern uint32_t systick_ms_now(void); /* from systick_ms_rt1062.h -- see prior comment */

/* Was my_isr() -- now a HAL adapter callback (see peripherals.h's
 * `extern void PPS_Handler(void *param);` declaration, which this
 * function satisfies). Trigger mode is rising-edge only (confirmed),
 * not the both-edges toggle this file's earlier placeholder assumed --
 * but toggling s_pps_state once per callback invocation still gives
 * pps_edge_detected() in app_loop.c exactly one detected edge per PPS
 * pulse either way, so no downstream logic changes are needed for
 * this trigger-mode correction.
 *
 * CONFIRMED distinct from ds3231_rt1062.c's TIMEPULSE_Handler (GPIO1
 * pin 3, falling edge, the DS3231's own seconds pulse) -- this is
 * GPIO1 pin 2, rising edge, the GPS module's satellite-derived PPS.
 * Two genuinely separate 1-second signals; the shared-combined-vector
 * collision flagged earlier between these two files is fully resolved
 * now that they're on different pins with the HAL adapter dispatching
 * each independently. */
void PPS_Handler(void *param)
{
    (void)param;
    s_gps_time_from_rabbit_ms = systick_ms_now();
    s_gps_tick++;
    s_pps_state ^= 1;
}

uint32_t neo_m8t_gps_get_tick_count(void)
{
    return s_gps_tick;
}

int neo_m8t_gps_get_pps_state(void)
{
    return s_pps_state;
}

/* ------------------------------------------------------------------ */

static void rt1062_cs_enable(void *ctx)
{
    (void)ctx;
    GPIO_PinWrite(NEO_CS_GPIO, NEO_CS_PIN, 0u); /* active low, matches original CS2_ENABLE */
}

static void rt1062_cs_disable(void *ctx)
{
    (void)ctx;
    GPIO_PinWrite(NEO_CS_GPIO, NEO_CS_PIN, 1u);
}

static void rt1062_delay_ms(void *ctx, uint32_t ms)
{
    (void)ctx;
    /* Was passing NEO_LPSPI_CLK_FREQ_HZ (the peripheral's clock) here --
     * SDK_DelayAtLeastUs's second argument should be the CORE clock
     * (SystemCoreClock), not a peripheral's. Fixed. */
    SDK_DelayAtLeastUs(ms * 1000u, SystemCoreClock);
}

static void rt1062_transfer(void *ctx, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len)
{
    rt1062_neo_hw_ctx_t *hw = (rt1062_neo_hw_ctx_t *)ctx;
    static uint8_t padded_tx[2000]; /* was the original's fixed 2000-byte
        SPI transaction size for both SPIWrRd() and SPIRead() -- LPSPI's
        blocking transfer needs one txData buffer of the SAME length as
        rxData, so the real command bytes (if any) are copied in here
        followed by 0xFF dummy filler for the remainder, matching how
        SPI full-duplex naturally clocks dummy bytes out while reading
        a longer response than the command that triggered it. */
    lpspi_transfer_t xfer = {0};
    status_t status;

    if (rx_len > sizeof(padded_tx)) {
        return; /* TODO: handle gracefully -- shouldn't happen given
                    UBX_RESPONSE_SIZE==2000 matches this buffer exactly */
    }

    if (tx != NULL && tx_len > 0) {
        memcpy(padded_tx, tx, tx_len);
    } else {
        tx_len = 0;
    }
    memset(padded_tx + tx_len, 0xFF, rx_len - tx_len);

    xfer.txData = padded_tx;
    xfer.rxData = rx;
    xfer.dataSize = rx_len;
    /* Was kLPSPI_MasterPcs1 (brief hardware-PCS detour). Reverted to
     * manual CS control (see file header) -- continuous transfer, no
     * PCS-selection flag needed since CS is handled entirely outside
     * the LPSPI peripheral now via cs_enable()/cs_disable(). */
    xfer.configFlags = kLPSPI_MasterPcsContinuous;

    status = LPSPI_MasterTransferBlocking(hw->base, &xfer);
    (void)status; /* TODO: original SPIWrRd()/SPIRead() had no error
                      return path to propagate here either -- flagged,
                      not silently assumed fine */
}

neo_m8t_transport_t neo_m8t_transport_rt1062_init(void)
{
    neo_m8t_transport_t t;

    /* LPSPI3 bus init (instance, baud, mode) is NOT done here -- 
     * BOARD_InitPeripherals() already does it via LPSPI3_init(), since
     * this is a shared bus with the nRF52833. Calling
     * LPSPI_MasterInit() here too would reconfigure a bus another
     * device also depends on. CS, however, IS this file's
     * responsibility again (manual GPIO, not the peripheral's PCS) --
     * see file header. */

    s_hw_ctx.base = NEO_LPSPI_BASE;

    {
        gpio_pin_config_t cs_config = {kGPIO_DigitalOutput, 1, kGPIO_NoIntmode};
        GPIO_PinInit(NEO_CS_GPIO, NEO_CS_PIN, &cs_config);
    }

    /* PPS pin init/interrupt-enable/callback-install is no longer done
     * here -- BOARD_InitPeripherals() (MCUXpresso Config Tools
     * generated, peripherals.c) already does all of that for
     * GPIO1_PPS. This function only needs to provide PPS_Handler()'s
     * body (see above), not configure the pin itself. Make sure
     * BOARD_InitPeripherals() actually runs before anything calls
     * neo_m8t_transport_rt1062_init() or gps_configure_timepulse(). */

    t.ctx = &s_hw_ctx;
    t.cs_enable = rt1062_cs_enable;
    t.cs_disable = rt1062_cs_disable;
    t.delay_ms = rt1062_delay_ms;
    t.transfer = rt1062_transfer;
    return t;
}
