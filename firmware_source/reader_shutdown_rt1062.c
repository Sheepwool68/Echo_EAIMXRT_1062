/*
 * reader_shutdown_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- pins CONFIRMED (see reader_shutdown_rt1062.h). Uses the raw
 * fsl_gpio.h driver, not the HAL adapter, since these pins aren't part of
 * the MCUXpresso Config Tools-generated peripherals.c/pin_mux.c HAL-adapter
 * set shown so far -- if you add them there later, switch this to match
 * ds3231_rt1062.c/neo_m8t_transport_rt1062.c's HAL adapter pattern instead.
 * ==========================================================================
 */

#include "reader_shutdown_rt1062.h"
#include "fsl_gpio.h"
#include "fsl_common.h"

#define READER_SHUTDOWN_GPIO    GPIO3
#define READER_SHUTDOWN_PIN     15u

#define READER_PWR_GPIO         GPIO3
#define READER_PWR_PIN          17u

void reader_shutdown_rt1062_init(void)
{
    /* Default state at boot: reader OFF on both pins (SHUTDOWN high,
     * PWR low) -- a safe power-up default until something explicitly
     * calls reader_power_set(1). */
    gpio_pin_config_t shutdown_config = { kGPIO_DigitalOutput, 1, kGPIO_NoIntmode };
    gpio_pin_config_t pwr_config = { kGPIO_DigitalOutput, 0, kGPIO_NoIntmode };

    GPIO_PinInit(READER_SHUTDOWN_GPIO, READER_SHUTDOWN_PIN, &shutdown_config);
    GPIO_PinInit(READER_PWR_GPIO, READER_PWR_PIN, &pwr_config);
}

void reader_shutdown_set(int enable)
{
    /* Active low: enable=1 -> pin low (reader on); enable=0 -> pin high (reader off). */
    GPIO_PinWrite(READER_SHUTDOWN_GPIO, READER_SHUTDOWN_PIN, enable ? 0u : 1u);
}

void reader_pwr_set(int enable)
{
    /* Active high: enable=1 -> pin high (reader on); enable=0 -> pin low (reader off). */
    GPIO_PinWrite(READER_PWR_GPIO, READER_PWR_PIN, enable ? 1u : 0u);
}

void reader_power_set(int enable)
{
    /* Was: reader_shutdown_set(enable); reader_pwr_set(enable); --
     * REDUCED to READER_SHUTDOWN only, 2026-07-17, per explicit
     * instruction: READER_PWR is now used to drive the button LED
     * instead (see button_led_rt1062.c, which calls reader_pwr_set()
     * directly for that) -- this particular board has no real
     * reader-power-control circuit connected to that pin anyway ("This
     * board... does not have reader power control. Future one will."),
     * so writing it here for that original purpose was already moot.
     * A future board WITH real reader power control would need this
     * restored to writing both pins together again AND the LED moved
     * off READER_PWR to a different pin -- same tradeoff already made
     * for MODEM_PWR, see gprs_transport_rt1062.c. */
    reader_shutdown_set(enable);
}
