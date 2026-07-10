/*
 * fan_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- pin and polarity CONFIRMED (see fan_rt1062.h), not compiled/
 * tested against real hardware here.
 * ==========================================================================
 *
 * CONFIRMED: active high (pin high = fan on). Trigger CONFIRMED: fan
 * turns on when the reader starts and off when it stops -- see
 * app_pc_dispatch.c's app_uhf_reader_control() (was UHF_Reader_Control()),
 * where fan_on()/fan_off() are now called.
 *
 * NOT CONFIRMED: HAL_GpioSetOutput's actual name/signature -- same flag
 * as buzzer_rt1062.c and gprs_transport_rt1062.c's GPIO writes, all of
 * which have the identical gap (no output-write call example was ever
 * shown in peripherals.c -- only input+callback pins were).
 */

#include "fan_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated --
    BOARD_INITPINS_FAN_CONTROL_handle */
#include "fsl_common.h"

void fan_rt1062_init(void)
{
    /* Nothing to do -- BOARD_InitPeripherals() already initializes
     * this pin via the HAL adapter. */
}

void fan_on(void)
{
    HAL_GpioSetOutput(BOARD_INITPINS_FAN_CONTROL_handle, 1u);
}

void fan_off(void)
{
    HAL_GpioSetOutput(BOARD_INITPINS_FAN_CONTROL_handle, 0u);
}
