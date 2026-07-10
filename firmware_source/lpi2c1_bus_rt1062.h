/*
 * lpi2c1_bus_rt1062.h
 *
 * Shared LPI2C1 bus helper for the DS3231/MP2731/MAX17303 drivers, which
 * all sit on the same physical I2C bus (confirmed: peripherals.c's
 * LPI2C1_init() initializes this bus once via BOARD_InitPeripherals()).
 *
 * CONFIRMED from your own tested I2C.c: this project's real LPI2C usage
 * pattern is LPI2C_MasterTransferNonBlocking() + a completion callback +
 * a spin-wait on completion/NACK flags -- NOT LPI2C_MasterTransferBlocking(),
 * which every I2C driver file in this port originally used. This file
 * replaces that with the confirmed-working pattern, in one shared place
 * instead of three separate copies of the same callback/flag machinery
 * (I2C is inherently single-transfer-at-a-time on one bus anyway, so a
 * single shared handle/flags is both correct and simpler than one per
 * device).
 *
 * Also matches your I2C.c in NOT reusing peripherals.c's own
 * LPI2C1_masterHandle (which peripherals.c creates with a NULL callback)
 * -- this file creates its own handle via its own
 * LPI2C_MasterTransferCreateHandle() call, exactly like your I2C.c's
 * Init_I2C() does, since that's the version actually confirmed working.
 */

#ifndef LPI2C1_BUS_RT1062_H
#define LPI2C1_BUS_RT1062_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at startup, before any device driver on this bus
 * (ds3231_rt1062_init(), mp2731_rt1062_init(), max17303_rt1062_init())
 * runs. Does NOT call LPI2C_MasterInit() -- that's peripherals.c's
 * LPI2C1_init() job via BOARD_InitPeripherals(), already confirmed to
 * run first. Only creates the non-blocking transfer handle + callback. */
void lpi2c1_bus_rt1062_init(void);

/*
 * Was the I2C_function_write()/I2C_function_read() pattern from your
 * I2C.c, generalized to arbitrary dataSize/direction rather than two
 * separate functions. direction: 0 = write, 1 = read (matches
 * kLPI2C_Write/kLPI2C_Read without pulling fsl_lpi2c.h's types into
 * this header). Returns 0 on success, -1 on failure (transfer
 * rejected or NACKed).
 */
int lpi2c1_bus_transfer(uint8_t slave_address, int direction, uint8_t subaddress,
                         uint8_t *data, size_t data_size);

#ifdef __cplusplus
}
#endif

#endif /* LPI2C1_BUS_RT1062_H */
