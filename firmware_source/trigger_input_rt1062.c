/*
 * trigger_input_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- pin CONFIRMED (see trigger_input_rt1062.h), not compiled/
 * tested against real hardware here.
 * ==========================================================================
 *
 * NOT CONFIRMED: HAL_GpioGetInput's actual name/signature -- nothing
 * pasted so far has shown a GPIO input-read call example either (only
 * output-write and callback-based input were shown). Best-effort guess.
 */

#include "trigger_input_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated --
    BOARD_INITPINS_TRIGGER_handle */
#include "fsl_common.h"

void trigger_input_rt1062_init(void)
{
    /* Nothing to do -- BOARD_InitPeripherals() already initializes
     * this pin (direction, trigger mode) via the HAL adapter. */
}

int trigger_input_read(void)
{
    uint8_t level = 0;
    HAL_GpioGetInput(BOARD_INITPINS_TRIGGER_handle, &level);
    return (int)level;
}
