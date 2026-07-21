/*
 * max17303_fuel_gauge_rt1062.h
 *
 * ==========================================================================
 * SCAFFOLD -- same tier as ds3231_rt1062.c/mp2731_charger_rt1062.c.
 * Register map, dual-address scheme, and 16-bit little-endian register
 * format all confirmed from the just-pasted I2C_SW.LIB source (max_read/
 * max_write).
 * ==========================================================================
 *
 * TWO I2C ADDRESSES, per the pasted source's own comments: ADDRESS1
 * (0x6C) for the regular runtime data registers (REPCAP, VCELL, CURRENT,
 * etc); ADDRESS2 (0x16) for the "n"-prefixed non-volatile configuration
 * registers (nVPrtTh1/OVP, nIPrtTh1/SOVCP, nODSCTh/FOVCP, etc -- visible
 * from the register names themselves in the pasted source). This
 * matches known Maxim/Analog Devices ModelGauge m5 EZ-family fuel gauge
 * conventions, where NV config registers require access via a secondary
 * I2C address distinct from the main data memory map -- NOT a ">0xFF
 * address extension" as the source's own terse comment might suggest at
 * first read (all the listed register offsets are within one byte's
 * range either way).
 *
 * 16-BIT REGISTER FORMAT: confirmed little-endian (low byte first) from
 * max_read()/max_write()'s explicit byte ordering.
 *
 * HONESTY NOTE, same as MP2731: only the register-level read/write
 * primitives were in the pasted source. Conversion formulas (e.g.
 * current/voltage LSB scaling) and any NV-register unlock sequence
 * Maxim's datasheet requires before writing "n"-prefixed registers
 * aren't shown here -- I don't have verified access to whatever
 * main()-level code applied those. This module gives you faithfully-
 * ported read/write primitives and the register map; scaling formulas
 * and NV-write sequencing are a TODO against the actual MAX17303
 * datasheet, or by pasting the relevant original code.
 */

#ifndef MAX17303_FUEL_GAUGE_RT1062_H
#define MAX17303_FUEL_GAUGE_RT1062_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX17303_ADDR_MAIN  0x36u
#define MAX17303_ADDR_NV    0x0Bu

/* Main data registers (use MAX17303_ADDR_MAIN) */
#define MAX17303_REG_REPCAP        0x05
#define MAX17303_REG_REPSOC        0x06
#define MAX17303_REG_MAXMIN_CURR   0x0A
#define MAX17303_REG_TTF           0x20
#define MAX17303_REG_VCELL         0x1A
#define MAX17303_REG_CURRENT       0x1C
#define MAX17303_REG_AV_CURRENT    0x1D
#define MAX17303_REG_CHARGE_CURRENT 0x28
/* CORRECTED (confirmed from program_init(): `max_write(MAX17303_ADDRESS1,
 * MAX17303_OVP_BAK, ...)`) -- despite its high offset suggesting it
 * belongs with the NV-protected group below, OVP_BAK is accessed via
 * the MAIN address, not the NV address. An earlier version of this
 * header had it miscategorized under the NV group; fixed now that
 * real calling code confirms which address it actually uses. Read as
 * a shadow/backup copy of the OVP threshold living in the
 * always-accessible main register space, distinct from the
 * NV-protected nVPrtTh1 (MAX17303_REG_OVP) itself. */
#define MAX17303_REG_OVP_BAK       0xD6
/* Board-revision detection register -- confirmed from program_init():
 * `max_read(MAX17303_ADDRESS1, 0x21)`, checked against an expected
 * value of 0x4067 to detect "V3.2 boards". No official register name
 * was given anywhere in the pasted source (just the literal 0x21) --
 * this name is this port's own label for what it's USED for here, not
 * a documented MAX17303 register name. Rename it if you have the
 * datasheet's actual name for register 0x21. */
#define MAX17303_REG_BOARD_DETECT  0x21
/* CORRECTED 2026-07-16, same pattern as OVP_BAK above -- confirmed
 * from the ONE real call site in the whole reference source
 * (ACTIVERFID_V1.02_UHF.c line 1740, myGenieEventHandler()'s
 * GENIE_REMOTE case): `max_write(MAX17303_ADDRESS1, MAX17303_FAULTS,
 * 0x00)`. Despite its offset sitting right in the middle of the
 * NV-protected group below (which is grouped by offset range, not by
 * confirmed call site), FAULTS is accessed via the MAIN address, not
 * NV -- moved up out of that group now that real calling code confirms
 * it, not just offset-proximity guessing. */
#define MAX17303_REG_FAULTS        0xAF

/* NV/protected config registers (use MAX17303_ADDR_NV) */
#define MAX17303_REG_OVP           0xD0
#define MAX17303_REG_JEITAC        0xD8
#define MAX17303_REG_SOVCP         0xD3
#define MAX17303_REG_FOVCP         0xDD
#define MAX17303_REG_RESISTOR      0xCF
#define MAX17303_REG_DELAY         0xDC
#define MAX17303_REG_NVEMPTY       0x9E
#define MAX17303_REG_NPACKCFG      0xB5

int max17303_rt1062_init(void);

int max17303_read_reg(uint8_t slave_addr, uint8_t reg_address, uint16_t *out_value);

int max17303_write_reg(uint8_t slave_addr, uint8_t reg_address, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* MAX17303_FUEL_GAUGE_RT1062_H */
