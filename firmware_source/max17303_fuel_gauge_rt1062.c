/*
 * max17303_fuel_gauge_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- transfer pattern CONFIRMED against your own tested I2C.c
 * (non-blocking transfer + callback + spin-wait, via the shared
 * lpi2c1_bus_rt1062.c helper). Not compiled/tested against real hardware
 * in this sandbox.
 * ==========================================================================
 *
 * Was using LPI2C_MasterTransferBlocking() directly, and its own separate
 * LPI2C_MasterInit() call -- both replaced: this bus is shared with
 * DS3231/MP2731 (confirmed via peripherals.c's single LPI2C1_init()),
 * and the confirmed-working transfer pattern is non-blocking+callback,
 * not blocking. See lpi2c1_bus_rt1062.h.
 */

#include "max17303_fuel_gauge_rt1062.h"
#include "lpi2c1_bus_rt1062.h"

int max17303_rt1062_init(void)
{
    /* Was its own LPI2C_MasterGetDefaultConfig()+LPI2C_MasterInit() --
     * removed. peripherals.c's LPI2C1_init() (via BOARD_InitPeripherals())
     * already initializes this shared bus; lpi2c1_bus_rt1062_init()
     * (called once, separately -- see app_init.c) sets up the
     * non-blocking transfer handle this driver uses. Nothing left for
     * this function to do, kept as a real function so existing call
     * sites don't need to change. */
    return 0;
}

int max17303_read_reg(uint8_t slave_addr, uint8_t reg_address, uint16_t *out_value)
{
    uint8_t raw[2];

    if (lpi2c1_bus_transfer(slave_addr, 1, reg_address, raw, 2) != 0) {
        return -1;
    }

    *out_value = (uint16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
    return 0;
}

int max17303_write_reg(uint8_t slave_addr, uint8_t reg_address, uint16_t value)
{
    /* Was manually embedding reg_address as buf[0] with subaddressSize=0
     * -- now uses the SDK's native subaddress mechanism via the shared
     * helper, matching your confirmed-working I2C.c exactly (same fix
     * applied to ds3231_rt1062.c/mp2731_charger_rt1062.c). NOTE: the
     * MAX17303 register data itself is still sent little-endian
     * (low byte first) as a 2-byte payload after the subaddress,
     * matching this file's original byte order exactly -- only the
     * subaddress mechanism changed, not the payload encoding. */
    uint8_t buf[2];
    buf[0] = (uint8_t)(value & 0xFFu);
    buf[1] = (uint8_t)((value >> 8) & 0xFFu);
    return lpi2c1_bus_transfer(slave_addr, 0, reg_address, buf, sizeof(buf));
}
