/*
 * button_led_rt1062.h
 *
 * ==========================================================================
 * BIDIRECTIONAL LED, confirmed 2026-07-17 per explicit instruction -- this
 * is a genuine 2-lead antiparallel LED, driven by two GPIOs together, not
 * one pin against a fixed ground:
 *   - "pin 1" = GPIO3 pin 16, via GPIO_SD_B0_04 (raw fsl_gpio.h PLUS an
 *     explicit IOMUXC_SetPinMux()/SetPinConfig() -- see button_led_rt1062.c's
 *     header comment for why that matters). CONFIRMED 2026-07-17 from the
 *     schematic reference the user provided -- "Led pins are
 *     GPIO_SD_B0_04-SD1_D2 and GPIO_SD_B0_05-SD1_D3", each
 *     hyphen-joined pair naming ONE pin by its two alternate names, not
 *     two separate pins (confirmed explicitly: "GPIO_SD_B0_05-SD1_D3 is
 *     one pin") -- AND independently confirmed against
 *     `drivers/fsl_iomuxc.h`'s own `IOMUXC_GPIO_SD_B0_04_GPIO3_IO16`
 *     macro (ALT5 = GPIO3_IO16, not a guess). Pin-1 identity went
 *     through FOUR rounds before landing here:
 *       1. GPIO3 pin 4 via raw fsl_gpio.h, no pin_mux.c check -- an
 *          early, never-independently-confirmed guess, abandoned after
 *          "not seeing led lit". (Coincidentally the right PIN NUMBER
 *          on a DIFFERENT pin -- see round 3's note below.)
 *       2. MODEM_PWR (GPIO1 pin 22) -- wrong, abandoned.
 *       3. GPIO3 pin 4 again, this time via a pre-existing
 *          `identifier: BUTTON_LED` label already sitting in
 *          `board/pin_mux.c` at `GPIO_SD_B1_04` (SD_**B1** bank) --
 *          looked authoritative (real generated
 *          IOMUXC_SetPinMux/SetPinConfig/GPIO_PinInit) but reported "no
 *          change": apparently a stale/unrelated label, not this LED's
 *          real pin.
 *       4. **CURRENT, CONFIRMED**: GPIO3 pin 16 via `GPIO_SD_B0_04`
 *          (SD_**B0** bank -- the schematic's literal reading all
 *          along). This exact pin/number was tried once before (an
 *          earlier attempt in this same debugging arc) using ONLY
 *          `GPIO_PinInit()` (raw fsl_gpio.h) with no IOMUXC mux call --
 *          `GPIO_PinInit()` never touches the pad's mux-select, and
 *          `GPIO_SD_B0_04` has NO entry anywhere in `pin_mux.c` (unlike
 *          READER_PWR/READER_SHUTDOWN, which do), so the pad stayed on
 *          its power-on-reset ALT0 default (`USDHC1_DATA2`) the whole
 *          time -- GPIO3 writes had zero effect on the physical pin,
 *          exactly matching a floating ~1.2V multimeter reading instead
 *          of a real logic-level swing. Fixed by adding the missing
 *          `IOMUXC_SetPinMux()`/`IOMUXC_SetPinConfig()` calls directly
 *          in `button_led_rt1062_init()` (see that file).
 *   - "pin 2" = READER_PWR (GPIO3 pin 17, GPIO_SD_B0_05 -- CONFIRMED
 *     correct by the schematic reference AND already present/correct in
 *     `pin_mux.c`. See reader_shutdown_rt1062.h/.c, the SAME real,
 *     already-confirmed-working pin used for UHF reader power control,
 *     driven here via its existing reader_pwr_set() primitive rather
 *     than a separate raw GPIO driver).
 *
 * MODEM_PWR (GPIO1 pin 22) is NOT involved in this LED at all --
 * gprs_transport_rt1062.c's rt1062_gprs_set_power_enable() has been
 * restored to its original real functionality (see that file's own
 * comment for the full history of the mix-up).
 *
 * IMPORTANT, per earlier explicit instruction (still applies to pin 2):
 * this is a per-board wiring choice, not a universal one. A FUTURE board
 * may need READER_PWR restored to real reader power control (undoing
 * reader_power_set()'s no-op there) -- if so, this LED needs pin 2 moved
 * to a genuinely free pin instead (pin 1, GPIO3 pin 16, was never shared
 * with anything else, so no equivalent concern there).
 *
 * Truth table (both pins driven together, never independently):
 *   pin1=HIGH, pin2=LOW  -> red   (button_led_red())
 *   pin1=LOW,  pin2=HIGH -> green (button_led_green() -- was the original's
 *                            only-ever-used lit state, `BitWrPortI(PBDR,
 *                            &PBDRShadow,1,6); //green`)
 *   pin1=LOW,  pin2=LOW  -> off   (button_led_off(), no potential
 *                            difference across the LED)
 * Added 2026-07-17 per explicit test request ("red lit on bootup, green
 * when we have time sync, green flashing for reader reading") -- see
 * app_init.c (boot -> red), app_loop.c's DS3231 rollover block (first
 * time sync -> solid green; while reading -> green/off blink), and
 * app_pc_dispatch.c (stop reading -> solid green) for where each state
 * is driven. This ALSO serves as a hardware self-test: if red/green come
 * up swapped or the wrong pin combination doesn't light at all, that's
 * immediate, visible evidence about which assumption in this header is
 * wrong (see the polarity caveat below) -- not something to guess at
 * further from software alone.
 *
 * Which physical color each combination actually produces, and whether
 * the assumed polarity is even the right way around, is NOT
 * independently confirmed against real hardware -- swap
 * button_led_red()'s/button_led_green()'s levels (or swap which function
 * is called where) if the real board lights the opposite color for a
 * given combination.
 * ==========================================================================
 *
 * Was the "button LED" (PB6 in the original) -- lit on boot, solid lit
 * whenever the reader is idle (not reading), and blinking once per second
 * whenever ProgramState==READING (ACTIVERFID_V1.02_UHF.c lines 3456, 1535,
 * 3600-3609). The red-on-boot/green-once-synced distinction is a NEW
 * test-only refinement on top of that original behavior, per explicit
 * instruction -- the original never distinguished red from green (it only
 * ever lit the one color), so there's no source position to match for
 * exactly when the red->green transition should happen; "once time sync
 * is first achieved" was chosen as the most direct, unambiguous reading
 * of "green when we have time sync".
 */

#ifndef BUTTON_LED_RT1062_H
#define BUTTON_LED_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

/* Configures both pins (GPIO3 pin 16 and pin 17/READER_PWR) as digital
 * outputs. Pin 1 (GPIO3 pin 16) has NO Config-Tools-generated
 * `pin_mux.c` support at all, so this function explicitly calls
 * `IOMUXC_SetPinMux()`/`IOMUXC_SetPinConfig()` itself before
 * `GPIO_PinInit()` -- see button_led_rt1062.c's header comment for why
 * that's required (the earlier "1.2V floating" symptom was exactly this
 * missing mux call). Pin 2's init is intentionally redundant with
 * reader_shutdown_rt1062_init() (which also configures that same pin) --
 * GPIO_PinInit() on an already-configured pin is harmless, and this
 * keeps button_led_rt1062_init() self-sufficient regardless of whether
 * it happens to run before or after reader_shutdown_rt1062_init() in
 * app_init() (see app_init.c's own re-assertion comment for the ordering
 * story). */
void button_led_rt1062_init(void);

/* Drives both pins to the "red" combination -- see this header's own
 * truth table above. */
void button_led_red(void);

/* Drives both pins to the "green" combination. */
void button_led_green(void);

/* Drives both pins to the "off" combination (no potential difference
 * across the LED). */
void button_led_off(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_LED_RT1062_H */
