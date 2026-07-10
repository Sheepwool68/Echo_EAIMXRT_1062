/*
 * trigger_input_rt1062.h
 *
 * Confirmed real pin: gpio_io.23, "TRIGGER" in the generated peripherals,
 * input, rising-edge trigger mode configured. Likely related to
 * Settings.TriggerOn and the trigger branch inside the original's
 * DS3231_isr() -- which was entirely commented out in what was pasted,
 * so this port previously assumed there might be nothing live to port
 * here. This real, actively-configured pin makes that assumption
 * questionable; not resolved yet.
 *
 * NOTABLE: peripherals.c calls HAL_GpioInit() and HAL_GpioSetTriggerMode()
 * for this pin, but NEVER HAL_GpioInstallCallback() (unlike PPS/TIMEPULSE,
 * which both get one) -- the trigger mode is configured but nothing
 * routes an edge to software yet. This suggests polling
 * (HAL_GpioGetInput) is the intended read method, or a callback meant
 * to be installed separately outside the generated code.
 *
 * This file only provides the raw read primitive -- no behavior for
 * "what happens when triggered" is implemented, deliberately, pending
 * clarification on what Settings.TriggerOn is actually supposed to do.
 */

#ifndef TRIGGER_INPUT_RT1062_H
#define TRIGGER_INPUT_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

void trigger_input_rt1062_init(void);

/* Was a direct read of the trigger pin's current level. Returns 1 or
 * 0. NOT an edge-detected "did a trigger happen" signal -- just the
 * instantaneous pin state; if you need edge detection, that's the
 * still-open design question referenced above. */
int trigger_input_read(void);

#ifdef __cplusplus
}
#endif

#endif /* TRIGGER_INPUT_RT1062_H */
