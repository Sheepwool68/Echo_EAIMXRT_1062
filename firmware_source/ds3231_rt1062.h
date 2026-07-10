#ifndef DS3231_RT1062_H
#define DS3231_RT1062_H

#include <stdint.h>
#include "rtc_time.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initializes LPI2C for the DS3231 and configures the SQW GPIO input
 * with a falling-edge interrupt. Also explicitly forces 24-hour mode
 * (see rtc_time.h note on ds3231_regs_t) and enables the 1Hz square
 * wave output on the DS3231 itself (control register: BBSQW off,
 * INTCN=0, RS bits select 1Hz) -- was `IntSqw_Set(0x40)` +
 * `RTC_Write_Reg(DS3231_REG_STATUS/CONTROL,...)` in the original.
 *
 * Call once at startup.
 */
int ds3231_rt1062_init(void);

/* Blocking I2C read of all 7 time registers + BCD/convention decode. */
int ds3231_rt1062_read(rtc_datetime_t *out);

/* Blocking I2C write of all 7 time registers. */
int ds3231_rt1062_write(const rtc_datetime_t *dt);

/*
 * Call once per main loop iteration (or from a dedicated FreeRTOS task
 * woken by the semaphore below). Returns 1 exactly once per DS3231 1Hz
 * edge, matching the original's ds_rollover flag consumption, with the
 * millisecond timestamp captured at the actual hardware edge (see
 * rtc_time.h / time_sync.h notes on why this replaces the original's
 * redundant second, coarser update).
 */
int ds3231_rt1062_poll_rollover(uint32_t *out_edge_timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* DS3231_RT1062_H */
