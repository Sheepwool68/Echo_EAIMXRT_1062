/*
 * mac_addr_rt1062.h -- see mac_addr_rt1062.c for the real source (EA's
 * own I2C EEPROM address/register), adapted to this port's LPI2C1
 * convention.
 */
#ifndef MAC_ADDR_RT1062_H
#define MAC_ADDR_RT1062_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reads the board's pre-programmed 6-byte EUI-48 MAC address from the
 * on-board I2C EEPROM into addr[0..5]. Falls back to a fixed default
 * address if the EEPROM read fails. */
void MAC_Read(uint8_t *addr);

#ifdef __cplusplus
}
#endif

#endif /* MAC_ADDR_RT1062_H */
