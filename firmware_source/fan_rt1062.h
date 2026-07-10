/*
 * fan_rt1062.h
 *
 * Was `BitWrPortI(PEDDR, &PEDDRShadow, 1, 5); //PE5 output for fan
 * control` in program_init() -- confirmed real pin now: gpio_io.20,
 * "FAN_CONTROL" in the generated peripherals, output, initial level 0.
 *
 * CONFIRMED: active high (pin high = fan on). CONFIRMED trigger: the
 * fan turns on when the reader starts and off when it stops -- see
 * app_pc_dispatch.c's app_uhf_reader_control() (was UHF_Reader_Control()).
 */

#ifndef FAN_RT1062_H
#define FAN_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

void fan_rt1062_init(void);

void fan_on(void);
void fan_off(void);

#ifdef __cplusplus
}
#endif

#endif /* FAN_RT1062_H */
