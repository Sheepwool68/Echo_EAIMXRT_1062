/*
 * button_led_rt1062.c
 *
 * ==========================================================================
 * BIDIRECTIONAL LED -- see button_led_rt1062.h's header comment for the
 * full truth table and pin-correction history. Two GPIOs drive this one
 * LED together: pin 1 = GPIO3 pin 16 (GPIO_SD_B0_04, raw fsl_gpio.h PLUS
 * an explicit IOMUXC mux/pad config -- see below), pin 2 = READER_PWR
 * (GPIO3 pin 17, repurposed from UHF reader power control -- see
 * reader_shutdown_rt1062.c/.h, whose reader_pwr_set() primitive is
 * reused directly here rather than duplicating a separate raw GPIO
 * driver for the same pin).
 * ==========================================================================
 */

#include "button_led_rt1062.h"
#include "reader_shutdown_rt1062.h" /* reader_pwr_set() (pin 2, GPIO3 pin 17) --
    same real, already-confirmed-working pin/primitive
    reader_shutdown_rt1062.c uses for UHF reader power control. */
#include "fsl_gpio.h"
#include "fsl_common.h"
#include "fsl_iomuxc.h"
#include "debug_console_rt1062.h"

/* PRINTF redirect to LPUART5 -- see debug_console_rt1062.h. TEMPORARY
 * DIAGNOSTIC, added 2026-07-17 per explicit report ("not seeing led
 * lit") -- confirms whether these functions are actually being called
 * at all (and when), separating "software never reached this call" from
 * "software ran, but the physical LED still didn't respond". Left in
 * place through the pin-1 correction (MODEM_PWR -> GPIO3 pin 16, missing
 * IOMUXC mux) to confirm the fix on the next flash. Remove once the LED
 * is confirmed responding correctly. */
#undef PRINTF
/* SILENCED 2026-07-21, per explicit request ("printf on ethernet
 * comms only after boot"). Was `debug_printf`. Restore if this
 * tracing is wanted again. */
#define PRINTF(...) ((void)0)

/* GPIO_SD_B0_04 = GPIO3 pin 16, per the schematic reference ("Led pins
 * are GPIO_SD_B0_04-SD1_D2 and GPIO_SD_B0_05-SD1_D3", each
 * hyphen-joined pair naming ONE pin by its two alternate names -- e.g.
 * GPIO_SD_B0_05-SD1_D3 is the SAME single pin as READER_PWR, confirmed
 * 2026-07-17), and independently confirmed against the real SDK header
 * (`drivers/fsl_iomuxc.h`): `IOMUXC_GPIO_SD_B0_04_GPIO3_IO16` is the
 * ALT5 mux setting that actually routes this physical pad to GPIO3_IO16.
 *
 * REAL ROOT CAUSE of the earlier "1.2V floating" symptom, found
 * 2026-07-17: this exact pin number (GPIO3 pin 16) was already tried
 * once before, using only `GPIO_PinInit()` (raw `fsl_gpio.h`) -- but
 * `GPIO_PinInit()` only configures the GPIO peripheral's own
 * direction/output registers, it does NOT touch the pad's IOMUXC
 * mux-select at all. `GPIO_SD_B0_04` has NO entry anywhere in
 * `board/pin_mux.c` (confirmed by direct search) -- unlike READER_PWR/
 * READER_SHUTDOWN, which DO have Config-Tools-generated
 * `IOMUXC_SetPinMux()` calls even though they use the same raw
 * `fsl_gpio.h` driver style as this file. So this pad was left on its
 * power-on-reset ALT0 default (`USDHC1_DATA2`, an SD-card data line, per
 * `fsl_iomuxc.h`) the entire time -- GPIO3 register writes had zero
 * effect on the physical pin, exactly matching a floating ~1.2V
 * multimeter reading instead of a real 0V/3.3V swing. A brief detour
 * (GPIO3 pin 4 via `GPIO_SD_B1_04`, following a pre-existing but
 * apparently-stale `BUTTON_LED` label already sitting in `pin_mux.c`)
 * was tried and reported "no change" -- consistent with that being
 * simply the wrong physical pin, not connected to this LED at all.
 * Fixed here by explicitly calling `IOMUXC_SetPinMux()` +
 * `IOMUXC_SetPinConfig()` ourselves in `button_led_rt1062_init()` below,
 * since this pin was never added to the Pins tool and so has no
 * generated `BOARD_InitPins()` support to rely on -- unlike pin 2. */
#define BUTTON_LED_PIN1_GPIO    GPIO3
#define BUTTON_LED_PIN1_PIN     16u

void button_led_rt1062_init(void)
{
    /* Pin 1 (GPIO3 pin 16 / GPIO_SD_B0_04) needs its own explicit
     * IOMUXC mux + pad config -- see this file's header comment above
     * for why: this pin has no Config-Tools-generated `pin_mux.c`
     * support at all, so nothing else in the project ever routes this
     * pad to GPIO. 0U selects ALT5 (GPIO3_IO16, per
     * `IOMUXC_GPIO_SD_B0_04_GPIO3_IO16` in fsl_iomuxc.h -- the macro's
     * own mux-value field, not a raw guess).
     *
     * Pad config BUMPED 2026-07-17 from an initial 0xB0U (no pull,
     * moderate DSE=6 drive) to 0x1079U (max drive strength DSE=7, fast
     * slew) after the mux fix alone still measured a weak ~1.2V on this
     * pin even while actively driven HIGH (`button_led_red()`) --
     * something on the board is loading this net harder than a moderate
     * push-pull drive can overcome (this pin is part of the SD_B0 card
     * interface group on this EVK-derived layout, per `board/pin_mux.c`'s
     * own `board: MIMXRT1060-EVKC` tag -- plausibly a pull resistor from
     * that unused SD card circuit). 0x1079U is not a fresh guess -- it's
     * the SAME pad config value this project's own `pin_mux.c` already
     * uses, hardware-confirmed working, for SPI_CS/SPI_SDO/LPSPI3_SCK
     * (all real push-pull digital outputs on this board). If the pin
     * still reads weak even at max drive strength, that points at a low-
     * impedance external load (e.g. a populated pull resistor or short)
     * that no pad config can push through -- a hardware/board issue, not
     * a software one. */
    IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_04_GPIO3_IO16, 0U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_SD_B0_04_GPIO3_IO16, 0x1079U);

    /* Pin 2 (READER_PWR) IS also configured by
     * reader_shutdown_rt1062_init() -- this call is intentionally
     * redundant with that, not a replacement for it: GPIO_PinInit() on
     * an already-configured pin is harmless, and duplicating it here
     * means this function doesn't depend on running after
     * reader_shutdown_rt1062_init() in app_init(). */
    gpio_pin_config_t led_config = { kGPIO_DigitalOutput, 0, kGPIO_NoIntmode };

    GPIO_PinInit(BUTTON_LED_PIN1_GPIO, BUTTON_LED_PIN1_PIN, &led_config);
    PRINTF("Button LED: init done (pin1/GPIO3-16 mux+pad+gpio configured, pin2/READER_PWR configured as outputs)\r\n");
}

void button_led_red(void)
{
    /* pin1=HIGH, pin2=LOW -- the "red" combination, see
     * button_led_rt1062.h's truth table. reader_pwr_set() is active
     * high (enable=1 -> pin high), so pin2=LOW is reader_pwr_set(0). */
    GPIO_PinWrite(BUTTON_LED_PIN1_GPIO, BUTTON_LED_PIN1_PIN, 1u);
    reader_pwr_set(0);
    PRINTF("Button LED: RED (pin1=1, pin2=0)\r\n");
}

void button_led_green(void)
{
    /* pin1=LOW, pin2=HIGH -- the "green" combination -- was the
     * original's only-ever-used lit state. */
    GPIO_PinWrite(BUTTON_LED_PIN1_GPIO, BUTTON_LED_PIN1_PIN, 0u);
    reader_pwr_set(1);
    PRINTF("Button LED: GREEN (pin1=0, pin2=1)\r\n");
}

void button_led_off(void)
{
    /* pin1=LOW, pin2=LOW -- no potential difference across the LED. */
    GPIO_PinWrite(BUTTON_LED_PIN1_GPIO, BUTTON_LED_PIN1_PIN, 0u);
    reader_pwr_set(0);
    PRINTF("Button LED: OFF (pin1=0, pin2=0)\r\n");
}
