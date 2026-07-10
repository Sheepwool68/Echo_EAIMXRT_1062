/*
 * ds3231_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- LPI2C instance/clock and the seconds-pulse pin are now
 * CONFIRMED from peripherals.h/peripherals.c (MCUXpresso Config Tools
 * generated) plus your own clarification. Before relying on it:
 *   1. DS3231 I2C address (0x68) is fixed by the part itself.
 *   2. Add IOMUXC pin muxing for LPI2C1 SDA/SCL in your board's
 *      pin_mux.c (not duplicated here) -- the TIMEPULSE pin's own
 *      muxing is already handled by BOARD_InitPeripherals().
 *   3. CONFIRMED: STATUS=0x80 and the full CONTROL register sequence
 *      (including a genuine redundancy -- see ds3231_rt1062_init()'s
 *      comment) are confirmed directly from program_init().
 *   4. CONFIRMED: this chip's 1Hz square-wave output is GPIO1 pin 3,
 *      falling edge, called "TIMEPULSE" in the generated peripherals --
 *      NOT the same signal as GPS PPS (GPIO1 pin 2, rising edge,
 *      handled in neo_m8t_transport_rt1062.c). Two genuinely distinct
 *      1-second pulses: PPS is the GPS module's satellite-derived
 *      pulse, TIMEPULSE is the DS3231's own seconds pulse. This also
 *      fully resolves the shared-combined-vector collision flagged
 *      earlier between these two files -- they're on different pins
 *      now, and the HAL GPIO adapter dispatches each pin's callback
 *      independently regardless.
 *   5. Decide your ISR-to-task handoff mechanism: this file captures the
 *      timestamp and sets a flag directly in the callback (safe, matches
 *      the original's actual behavior), and provides a commented-out
 *      xSemaphoreGiveFromISR() call for projects using a dedicated
 *      FreeRTOS task instead of bare polling from the main loop.
 * ==========================================================================
 */

#include "ds3231_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated --
    GPIO1_TIMEPULSE_handle, LPI2C1_PERIPHERAL, LPI2C1_CLOCK_FREQ */

#include "lpi2c1_bus_rt1062.h"
#include "fsl_common.h"
#include "systick_ms_rt1062.h"

/* #include "FreeRTOS.h" */   /* uncomment if using the semaphore handoff below */
/* #include "semphr.h" */

/* -------------------------------------------------------------------
 * CONFIRMED from peripherals.h: LPI2C1, 60MHz source clock, 100kHz
 * baud (shared bus with MP2731/MAX17303, as this port already assumed
 * correctly). DS3231_LPI2C_BASE/BAUDRATE/CLK_FREQ_HZ below are now
 * informational only -- peripherals.c's LPI2C1_init() owns actually
 * configuring the bus; lpi2c1_bus_rt1062_init()/lpi2c1_bus_transfer()
 * are what this file calls into.
 * ---------------------------------------------------------------- */
#define DS3231_I2C_ADDR          0x68u   /* fixed by the DS3231 part itself */

/* DS3231 register addresses (from the datasheet -- standard across DS3231/DS3232) */
#define DS3231_REG_SECONDS   0x00
#define DS3231_REG_MINUTES   0x01
#define DS3231_REG_HOURS     0x02
#define DS3231_REG_DOW       0x03
#define DS3231_REG_DATE      0x04
#define DS3231_REG_MONTH     0x05
#define DS3231_REG_YEAR      0x06
#define DS3231_REG_CONTROL   0x0E
#define DS3231_REG_STATUS    0x0F

/* -------------------------------------------------------------------
 * ISR-to-main-loop handoff state.
 * volatile, accessed from both the callback and normal context -- this
 * is the direct equivalent of the original's iDSTimeFromRabbit/
 * ds_rollover globals set in DS3231_isr().
 * ---------------------------------------------------------------- */
static volatile uint32_t s_last_edge_timestamp_ms;
static volatile int s_rollover_pending;

/* -------------------------------------------------------------------
 * TIMEPULSE callback (was DS3231_isr() / my earlier placeholder ISR).
 * Was CALLED via a hand-written GPIOx_Combined_a_b_IRQHandler in an
 * earlier version of this file -- now the HAL adapter's callback
 * mechanism instead (see peripherals.h's `extern void
 * TIMEPULSE_Handler(void *param);` declaration, which this function
 * satisfies). BOARD_InitPeripherals() already handles pin init,
 * trigger-mode config (falling edge, confirmed), and callback
 * registration -- this file only provides the callback body.
 * ---------------------------------------------------------------- */
void TIMEPULSE_Handler(void *param)
{
    (void)param;

    s_last_edge_timestamp_ms = systick_ms_now();
    s_rollover_pending = 1;

    /* If using a dedicated FreeRTOS task instead of main-loop polling:
     *
     *   BaseType_t higher_priority_task_woken = pdFALSE;
     *   xSemaphoreGiveFromISR(s_rollover_semaphore, &higher_priority_task_woken);
     *   portYIELD_FROM_ISR(higher_priority_task_woken);
     *
     * Uncomment and wire up s_rollover_semaphore if you go that route.
     */
}

/* -------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------- */

int ds3231_rt1062_init(void)
{
    /*
     * CONFIRMED from program_init() (previously flagged as unverified):
     *
     *   IntSqw_Set(0x40);                          // 1 HZ
     *   RTC_Write_Reg(DS3231_REG_STATUS, 0x80);
     *   RTC_Write_Reg(DS3231_REG_CONTROL, 0x00);    // turn off BBSQW but turn on 1HZ SQW
     *
     * STATUS=0x80 is confirmed exactly as this port had already
     * guessed (hedge removed). But the CONTROL sequence has a real
     * quirk this port's earlier version got wrong: IntSqw_Set(0x40)
     * does a careful read-modify-write (mask 0xA3, OR 0x40, preserving
     * EOSC/CONV/A2IE/A1IE) -- but the very next line BLINDLY
     * overwrites the SAME register to 0x00, discarding everything
     * IntSqw_Set() just computed. The comment on that final write
     * ("turn off BBSQW but turn on 1HZ SQW") matches writing 0x00
     * exactly (EOSC=0, BBSQW=0, RS1=RS0=0, INTCN=0, A2IE=A1IE=0), so
     * 0x00 is genuinely the register's FINAL, EFFECTIVE state on real
     * hardware -- IntSqw_Set(0x40)'s bit-preservation is dead code
     * here, immediately clobbered. This port now performs BOTH steps
     * in the same order (not just the final state) for bit-for-bit
     * fidelity to the actual call sequence, not just its end result --
     * matching this port's established preserve-quirks-don't-silently-
     * simplify discipline. If you ever remove the later blind
     * overwrite from your own firmware, the read-modify-write's
     * preserved bits would then actually matter.
     */
    const uint8_t status_val = 0x80;
    const uint8_t control_preserve_mask = 0xA3u;
    const uint8_t control_or_value = 0x40u;
    const uint8_t control_final_value = 0x00u;

    /* TODO: pin muxing for LPI2C1 SDA/SCL must happen in pin_mux.c
     * before this runs -- not duplicated here. TIMEPULSE pin muxing,
     * init, trigger-mode config, and callback registration are
     * already handled by BOARD_InitPeripherals(); make sure that runs
     * before this function. */

    /* Was LPI2C_MasterGetDefaultConfig()+LPI2C_MasterInit() here --
     * removed. peripherals.c's LPI2C1_init() (via BOARD_InitPeripherals())
     * already initializes this shared bus; re-initializing it here
     * would redundantly reconfigure a bus MP2731/MAX17303 also depend
     * on. lpi2c1_bus_rt1062_init() (called once, separately -- see
     * app_init.c) sets up the non-blocking transfer handle this
     * driver now uses via lpi2c1_bus_transfer(), matching your
     * confirmed-working I2C.c's pattern instead of the blocking API
     * this file originally used. */

    /* Force 24-hour mode + 1Hz SQW output -- see rtc_time.h note on why
     * 24-hour mode specifically matters (the whole app assumes it). */
    {
        uint8_t current_control;
        uint8_t val;

        /* Step 1: IntSqw_Set(0x40) -- read-modify-write CONTROL.
         * Effect is fully overwritten by step 3 below, but performed
         * anyway for fidelity to the real call sequence (see comment
         * above). */
        if (lpi2c1_bus_transfer(DS3231_I2C_ADDR, 1, DS3231_REG_CONTROL, &current_control, 1) != 0) {
            return -1;
        }

        current_control &= control_preserve_mask;
        current_control |= control_or_value;

        val = current_control;
        if (lpi2c1_bus_transfer(DS3231_I2C_ADDR, 0, DS3231_REG_CONTROL, &val, 1) != 0) {
            return -1;
        }

        /* Step 2: RTC_Write_Reg(STATUS, 0x80). */
        val = status_val;
        if (lpi2c1_bus_transfer(DS3231_I2C_ADDR, 0, DS3231_REG_STATUS, &val, 1) != 0) {
            return -1;
        }

        /* Step 3: RTC_Write_Reg(CONTROL, 0x00) -- the BLIND overwrite
         * that actually determines the register's final state. */
        val = control_final_value;
        if (lpi2c1_bus_transfer(DS3231_I2C_ADDR, 0, DS3231_REG_CONTROL, &val, 1) != 0) {
            return -1;
        }
    }

    s_rollover_pending = 0;
    s_last_edge_timestamp_ms = 0;
    return 0;
}

int ds3231_rt1062_read(rtc_datetime_t *out)
{
    ds3231_regs_t regs;
    uint8_t raw[7];

    /* Was I2C_function_read(0x68, 0x00, 7) in your confirmed-working
     * I2C.c -- uses the SDK's own subaddress mechanism (native
     * subaddress+subaddressSize, not manually embedded in the data
     * buffer as this function did before). */
    if (lpi2c1_bus_transfer(DS3231_I2C_ADDR, 1, DS3231_REG_SECONDS, raw, sizeof(raw)) != 0) {
        return -1;
    }

    regs.sec   = raw[0];
    regs.min   = raw[1];
    regs.hour  = raw[2];
    regs.dow   = raw[3];
    regs.mday  = raw[4];
    regs.month = raw[5];
    regs.year  = raw[6];

    return ds3231_regs_to_datetime(&regs, out) ? 0 : -1;
}

int ds3231_rt1062_write(const rtc_datetime_t *dt)
{
    ds3231_regs_t regs;
    uint8_t buf[7];

    ds3231_datetime_to_regs(dt, &regs);

    buf[0] = regs.sec;
    buf[1] = regs.min;
    buf[2] = regs.hour;
    buf[3] = regs.dow;
    buf[4] = regs.mday;
    buf[5] = regs.month;
    buf[6] = regs.year;

    /* Was I2C_function_write(0x68, 0x00, ..., 7) in your confirmed-
     * working I2C.c -- same native-subaddress fix as the read side
     * above (was manually embedding DS3231_REG_SECONDS as buf[0]
     * with subaddressSize=0; now uses the SDK's own subaddress field,
     * matching your reference exactly). */
    return lpi2c1_bus_transfer(DS3231_I2C_ADDR, 0, DS3231_REG_SECONDS, buf, sizeof(buf));
}

int ds3231_rt1062_poll_rollover(uint32_t *out_edge_timestamp_ms)
{
    /* Snapshot with interrupts briefly disabled to avoid a torn read
     * against the ISR (both fields are written together, but without
     * a lock a rollover could occur between reading the two). */
    uint32_t primask = DisableGlobalIRQ();
    int pending = s_rollover_pending;
    uint32_t ts = s_last_edge_timestamp_ms;
    if (pending) {
        s_rollover_pending = 0;
    }
    EnableGlobalIRQ(primask);

    if (pending && out_edge_timestamp_ms != NULL) {
        *out_edge_timestamp_ms = ts;
    }
    return pending;
}
