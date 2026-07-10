/*
 * mp2731_charger_rt1062.c
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
 * DS3231/MAX17303 (confirmed via peripherals.c's single LPI2C1_init()),
 * and the confirmed-working transfer pattern is non-blocking+callback,
 * not blocking. See lpi2c1_bus_rt1062.h.
 */

#include "mp2731_charger_rt1062.h"
#include "lpi2c1_bus_rt1062.h"

int mp2731_rt1062_init(void)
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

int mp2731_read_reg(uint8_t reg_address, uint8_t *out_value)
{
    return lpi2c1_bus_transfer(MP2731_I2C_ADDR, 1, reg_address, out_value, 1);
}

int mp2731_write_reg(uint8_t reg_address, uint8_t value)
{
    /* Was manually embedding reg_address as buf[0] with subaddressSize=0
     * -- now uses the SDK's native subaddress mechanism via the shared
     * helper, matching your confirmed-working I2C.c exactly (same fix
     * applied to ds3231_rt1062.c). */
    uint8_t val = value;
    return lpi2c1_bus_transfer(MP2731_I2C_ADDR, 0, reg_address, &val, 1);
}
