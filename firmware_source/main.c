/*
 * main.c
 *
 * The actual entry point -- this was genuinely missing. app_context.h
 * declared app_init()/app_run_one_iteration(); this file is what
 * finally calls them from a real int main(void).
 *
 * ==========================================================================
 * REAL BOOT SEQUENCE, 2026-07-15 -- board-init calls corrected to match
 * this specific Config-Tools-generated project (the earlier draft's
 * `BOARD_InitHardware()`/standalone `BOARD_InitPeripherals()` assumed a
 * different SDK example template's function names; this project instead
 * generates BOARD_ConfigMPU()/BOARD_InitBootPins()/BOARD_InitBootClocks()/
 * BOARD_InitBootPeripherals() [which itself calls BOARD_InitPeripherals()
 * -- do not also call that separately, or LPI2C1/GPIO1/LPUART1/LPSPI3
 * would each init twice]/BOARD_InitDebugConsole(), confirmed by grepping
 * board/ in the real project). Sequence and rationale otherwise
 * unchanged from the original draft below.
 * ==========================================================================
 *
 * CURRENT bringup_config.h STATE: only APP_ENABLE_DISPLAY is on (see that
 * file's own "CURRENT STATE" note) -- this is a deliberately narrow first
 * pass to verify app_genie_dispatch.c's touch-event routing against real
 * hardware before bringing every other individually-confirmed subsystem
 * back up together for the first time.
 */

#include "board.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "peripherals.h"
#include "app_context.h"
#include "systick_ms_rt1062.h"
#include "lpuart5_console_rt1062.h"
#include "debug_console_rt1062.h"

/* PRINTF redirect to LPUART5 -- see debug_console_rt1062.h. Needed here
 * for this file's own boot-status prints below (app_init() failure
 * halt, etc); app_loop.c/lpi2c1_bus_rt1062.c have their own copies of
 * this same redirect since each translation unit needs it separately. */
#undef PRINTF
#define PRINTF debug_printf

static app_context_t g_app; /* static, not a main()-local: this struct is
                                large (holds every module's state) and
                                must live for the program's entire runtime,
                                not just main()'s stack frame */

int main(void)
{
    /* Real board-init sequence, confirmed from this project's own
     * board/ (Config Tools generated) -- matches the exact order
     * hello_world.c used across every prior confirmed bring-up this
     * session. BOARD_InitBootPeripherals() calls BOARD_InitPeripherals()
     * internally (GPIO1_init/LPI2C1_init/LPUART1_init/LPSPI3_init) --
     * do not call BOARD_InitPeripherals() again separately. */
    BOARD_ConfigMPU();
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
    BOARD_InitDebugConsole();

    /* LPUART5 (FTDI USB-serial) must be up before the FIRST PRINTF/
     * debug_printf call in this file or anything app_init()/
     * app_run_one_iteration() call into -- matches hello_world.c's own
     * "moved here" note from the semihosting-removal session. Pin
     * muxing (BOARD_InitBootPins()) and the 80MHz UART_CLK_ROOT
     * (BOARD_InitBootClocks()) are already up by this point. */
    lpuart5_console_rt1062_init();

    /* MUST come after BOARD_InitBootClocks() -- see
     * systick_ms_rt1062.h's ordering note: the 1ms SysTick reload value
     * is computed from SystemCoreClock, which BOARD_InitBootClocks()
     * is what actually sets to the real value. Calling this first
     * would silently tick at the wrong rate rather than failing
     * loudly. */
    if (systick_ms_init() != 0) {
        PRINTF("systick_ms_init() FAILED -- halting\r\n");
        for (;;) {
            /* halt -- see the app_init() failure-policy note below;
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
        PRINTF("app_init() FAILED -- halting\r\n");
        for (;;) {
            /* halt */
        }
    }

    PRINTF("app_init() OK -- entering main loop\r\n");

    for (;;) {
        app_run_one_iteration(&g_app);
        /* No vTaskDelay, no RTOS yield -- this is a true bare-metal
         * super-loop, matching the original's for(;;) exactly, and
         * matching the original Dynamic C's own continuous
         * genieDoEvents()-style polling (per explicit instruction: the
         * main loop should constantly poll for serial data from the
         * LCD, not on a windowed/gated cadence). app_run_one_iteration()
         * itself has no internal delay either, so this loop calls
         * display_do_events() (via process_display_events(), gated on
         * APP_ENABLE_DISPLAY) every single pass with no artificial
         * pacing. If you later want a deliberate idle/power-saving
         * delay between iterations, add it here explicitly rather than
         * assuming one is needed -- the original had none. */
    }

    return 0; /* unreachable */
}
