/*
 * mac_addr_rt1062.c
 *
 * Reads the board's pre-programmed EUI-48 MAC address from the I2C
 * EEPROM on the shared LPI2C1 bus (same bus as DS3231/MP2731/MAX17303).
 * Address (0x53) and register (0xFA) are CONFIRMED from Embedded
 * Artists' own mac_addr.c (shipped in their SDK's lwip_dhcp/bm example,
 * which the user has already run successfully on this exact board) --
 * this is a real, board-specific EEPROM, not a guess.
 *
 * Adapted from that reference to this port's own I2C convention: uses
 * lpi2c1_bus_rt1062.c's shared non-blocking-transfer/callback/spin-wait
 * helper (matching DS3231/MP2731/MAX17303's own drivers) instead of the
 * reference's direct LPI2C_MasterTransferBlocking() calls -- see
 * lpi2c1_bus_rt1062.h's header comment for why this port avoids the
 * blocking API.
 */

#include "mac_addr_rt1062.h"
#include "lpi2c1_bus_rt1062.h"
#include <stdint.h>

#define MAC_EEPROM_I2C_ADDR   0x53u
#define MAC_EEPROM_REG        0xFAu

void MAC_Read(uint8_t *addr)
{
    /* direction=1 (read): lpi2c1_bus_transfer() writes the subaddress
     * (register pointer) then reads data_size bytes back, matching the
     * original's "write 0xFA, then read 6 bytes" sequence in one call. */
    if (lpi2c1_bus_transfer(MAC_EEPROM_I2C_ADDR, 1, MAC_EEPROM_REG, addr, 6) != 0) {
        /* Same fallback default as the original -- used only if the
         * EEPROM read genuinely fails (wiring/address problem), not
         * expected in normal operation. */
        addr[0] = 0x00;
        addr[1] = 0x1A;
        addr[2] = 0xF1;
        addr[3] = 0x99;
        addr[4] = 0x99;
        addr[5] = 0x99;
    }
}
