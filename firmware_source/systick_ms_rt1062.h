/*
 * systick_ms_rt1062.h
 *
 * ==========================================================================
 * SCAFFOLD -- not compiled/tested in this environment, but HIGHER confidence
 * than most other hardware glue in this port: SysTick is architecturally
 * standard across every Cortex-M core (part of CMSIS-Core, not an
 * NXP-specific peripheral), so SysTick_Config()'s signature and behavior
 * are stable regardless of chip family.
 * ==========================================================================
 *
 * Provides the real millisecond tick source app_loop.c needs (previously
 * hardcoded to 0). Not part of the original firmware -- the original ran
 * on a Rabbit CPU with its own MS_TIMER hardware counter; this is new
 * infrastructure for the ARM target.
 *
 * ORDERING REQUIREMENT: call systick_ms_init() AFTER BOARD_InitHardware()
 * has run (specifically, after the clock configuration that sets the real
 * SystemCoreClock value) -- calling it before means the 1ms reload value
 * is computed against a stale/default core clock frequency, and every
 * timer-based feature in the firmware would silently run at the wrong
 * rate rather than failing loudly.
 */

#ifndef SYSTICK_MS_RT1062_H
#define SYSTICK_MS_RT1062_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int systick_ms_init(void);

uint32_t systick_ms_now(void);

void systick_ms_delay(uint32_t ms);

#ifdef __cplusplus
}
#endif

#endif /* SYSTICK_MS_RT1062_H */
