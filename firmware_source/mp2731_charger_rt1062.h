/*
 * mp2731_charger_rt1062.h
 *
 * ==========================================================================
 * SCAFFOLD -- not compiled/tested against real hardware here, same tier as
 * ds3231_rt1062.c. Confirmed against the just-pasted I2C_SW.LIB source:
 * slave address (0x96 = 7-bit 0x4B pre-shifted, per that file's own
 * comment "datasheet says 4B but idiots do not left shift 1"), full
 * register map, and the single-byte register read/write pattern (was
 * MP_Read()/MP_Write()) are all ported faithfully from that source.
 * ==========================================================================
 *
 * HONESTY NOTE, same as the DS3231 fix and ConnectToSocketServer: the
 * pasted I2C_SW.LIB only shows the register-level read/write primitives.
 * It does NOT show the main()/program_init()-level calling sequence --
 * which specific charge-current/voltage setpoints were written, in what
 * order, at startup. I don't have verified access to that (same
 * compaction issue as before). This module gives you a faithfully-ported
 * register map and read/write primitives; the actual charge-current/
 * voltage setpoint VALUES are a TODO for you to supply (from the MP2731
 * datasheet's register bit-field encoding, or by pasting the original
 * program_init() section that configures this chip).
 */

#ifndef MP2731_CHARGER_RT1062_H
#define MP2731_CHARGER_RT1062_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MP2731_I2C_ADDR       0x4Bu

#define MP2731_REG_CURRENT         0x00
#define MP2731_REG_NTC             0x02
#define MP2731_REG_ADC_CONT        0x03
#define MP2731_REG_CHARGE          0x04
#define MP2731_REG_CHARGE_CURRENT  0x05
#define MP2731_REG_CHARGE_VOLTAGE  0x07
#define MP2731_REG_TIMER           0x08
#define MP2731_REG_BATFET          0x0A
#define MP2731_REG_USB             0x0B
#define MP2731_REG_STATUS          0x0C
#define MP2731_REG_FAULT           0x0D
#define MP2731_REG_ADC             0x0E
#define MP2731_REG_TEMP            0x10
#define MP2731_REG_ADC_CURRENT     0x12
#define MP2731_REG_JEITA           0x16

int mp2731_rt1062_init(void);

int mp2731_read_reg(uint8_t reg_address, uint8_t *out_value);

int mp2731_write_reg(uint8_t reg_address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif /* MP2731_CHARGER_RT1062_H */
