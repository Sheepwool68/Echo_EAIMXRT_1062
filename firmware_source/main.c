/*
 * main.c
 *
 * The actual entry point -- this was genuinely missing. app_context.h
 * declared app_init()/app_run_one_iteration(); this file is what
 * finally calls them from a real int main(void).
 *
 * ==========================================================================
 * SCAFFOLD -- not compiled here (needs your board's SDK headers), same
 * tier as the other integration files. Structure is complete; the board
 * bring-up call at the top is a placeholder for whatever your MCUXpresso
 * project generates (clocks, pin muxing, etc) -- not this port's concern.
 * ==========================================================================
 */

#include "app_context.h"
#include "systick_ms_rt1062.h"

/* TODO: these three normally come from your MCUXpresso project's
 * generated board.h/clock_config.h/pin_mux.h. Declared here so this
 * file's intent is clear even before those are wired in. */
extern void BOARD_InitHardware(void);   /* clocks, pins, debug console, etc */

/* From peripherals.h/peripherals.c (MCUXpresso Config Tools generated --
 * not in this source tree, see INTEGRATION.md). THIS WAS MISSING: every
 * hardware driver in this port (lpi2c1_bus_rt1062.c, ds3231_rt1062.c,
 * TIMEPULSE, MP2731/MAX17303) assumed in comments that this had already
 * run before it touches its peripheral's registers, but nothing ever
 * actually called it -- LPI2C1's clock/pins were never brought up, so
 * the first non-blocking I2C transfer (APP_ENABLE_TIME_SYNC) hit
 * unclocked LPI2C1 registers and HardFaulted. Must run after
 * BOARD_InitHardware() (clocks must exist first) and before app_init()
 * (which starts touching LPI2C1 via lpi2c1_bus_rt1062_init()). */
extern void BOARD_InitPeripherals(void);

static app_context_t g_app; /* static, not a main()-local: this struct is
                                large (holds every module's state) and
                                must live for the program's entire runtime,
                                not just main()'s stack frame */

int main(void)
{
    BOARD_InitHardware();
    BOARD_InitPeripherals();

    /* MUST come after BOARD_InitHardware() -- see systick_ms_rt1062.h's
     * ordering note: the 1ms SysTick reload value is computed from
     * SystemCoreClock, which BOARD_InitHardware()'s clock config is
     * what actually sets to the real value. Calling this first would
     * silently tick at the wrong rate rather than failing loudly. */
    if (systick_ms_init() != 0) {
        for (;;) {
            /* halt -- see the app_init() failure-policy TODO below;
             * same "not a considered policy yet" caveat applies here. */
        }
    }

    if (app_init(&g_app) != 0) {
        /* Was effectively unreachable in the original -- main() didn't
         * check for init failure at all, since most of its setup calls
         * (LoadSettings, program_init, etc) had no failure path exposed.
         * This port's app_init() DOES report failure (a bad I2C/SPI/
         * network bring-up, for instance), so it needs handling here
         * rather than silently continuing into a half-initialized loop.
         * TODO: decide what "safe failure" means for your device --
         * flash an LED, log to a debug UART, watchdog-reset, etc. What's
         * here is a placeholder, not a considered failure policy. */
        for (;;) {
            /* halt */
        }
    }

    for (;;) {
        app_run_one_iteration(&g_app);
        /* No vTaskDelay, no RTOS yield -- this is a true bare-metal
         * super-loop, matching the original's for(;;) exactly. If you
         * later want a deliberate idle/power-saving delay between
         * iterations, add it here explicitly rather than assuming one
         * is needed -- the original had none. */
    }

    return 0; /* unreachable */
}
