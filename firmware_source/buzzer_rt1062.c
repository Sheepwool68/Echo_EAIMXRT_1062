/*
 * buzzer_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- pin now CONFIRMED from peripherals.h/peripherals.c
 * (MCUXpresso Config Tools generated). Not compiled/tested against real
 * hardware here (no way to link the real HAL adapter in this sandbox), but
 * the pin/API mismatch risk that existed with the placeholder is gone.
 * ==========================================================================
 *
 * Was `BitWrPortI(PBDR, &PBDRShadow, 1, 2)` (PB2) inside Beep(). Off is
 * CONFIRMED as the same pin driven low.
 *
 * CONFIRMED real pin: gpio_io.21, "BUZZER" in the generated peripherals,
 * output, initial level 0. Goes through the HAL GPIO adapter
 * (BOARD_INITPINS_BUZZER_handle) instead of the raw fsl_gpio.h driver
 * this file used before -- BOARD_InitPeripherals() already calls
 * HAL_GpioInit() for this pin, so this file must NOT also call
 * GPIO_PinInit() on it.
 *
 * NOT CONFIRMED: the actual GPIO output-write function
 * (HAL_GpioSetOutput below) -- nothing pasted so far has shown an
 * output-write call example (only input+callback pins were shown in
 * peripherals.c). This name/signature is a best-effort guess based on
 * this adapter's typical shape, not confirmed against your actual
 * fsl_adapter_gpio.h -- same flag as gprs_transport_rt1062.c's modem
 * wake pin, which has the identical gap.
 */

#include "buzzer_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated --
    BOARD_INITPINS_BUZZER_handle */
#include "fsl_common.h"

void buzzer_rt1062_init(void)
{
    /* Nothing to do here anymore -- BOARD_InitPeripherals() already
     * initializes this pin (direction, initial level) via the HAL
     * adapter. Kept as a no-op function rather than removed entirely,
     * so app_init.c's existing call site doesn't need to change. */
}

void buzzer_on(void)
{
    HAL_GpioSetOutput(BOARD_INITPINS_BUZZER_handle, 1u);
}

void buzzer_off(void)
{
    HAL_GpioSetOutput(BOARD_INITPINS_BUZZER_handle, 0u);
}

void buzzer_beep_n_blocking(int count)
{
    int i;
    for (i = 0; i < count; i++) {
        buzzer_on();
        SDK_DelayAtLeastUs(50000U, SystemCoreClock);
        buzzer_off();
        if (i + 1 < count) {
            SDK_DelayAtLeastUs(50000U, SystemCoreClock);
        }
    }
}
