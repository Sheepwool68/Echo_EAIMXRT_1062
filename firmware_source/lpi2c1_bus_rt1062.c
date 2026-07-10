/*
 * lpi2c1_bus_rt1062.c
 *
 * ==========================================================================
 * SCAFFOLD -- pattern CONFIRMED against your own tested I2C.c (the
 * non-blocking-transfer + callback + spin-wait sequence below matches it
 * exactly). Not compiled/tested against real hardware in this sandbox.
 * ==========================================================================
 */

#include "lpi2c1_bus_rt1062.h"
#include "peripherals.h" /* MCUXpresso Config Tools generated -- LPI2C1_PERIPHERAL */
#include "fsl_lpi2c.h"
#include "fsl_common.h"
#include <stdbool.h>

static lpi2c_master_handle_t s_handle;
static volatile bool s_completion_flag;
static volatile bool s_nack_flag;
static lpi2c_master_transfer_t s_xfer;

/* Was lpi2c_master_callback() in your I2C.c -- identical logic. */
static void lpi2c1_bus_callback(LPI2C_Type *base, lpi2c_master_handle_t *handle,
                                 status_t status, void *userData)
{
    (void)base;
    (void)handle;
    (void)userData;

    if (status == kStatus_LPI2C_Nak) {
        s_nack_flag = true;
    } else {
        s_completion_flag = true;
        /* Was `PRINTF("Error occured during transfer!")` on a non-success,
         * non-NAK status -- no debug console wired up at this layer of
         * this port; the caller already gets a -1 return for this case,
         * see lpi2c1_bus_transfer() below. */
    }
}

void lpi2c1_bus_rt1062_init(void)
{
    /* Was Init_I2C() -- but WITHOUT the LPI2C_MasterInit() call, since
     * BOARD_InitPeripherals()'s LPI2C1_init() already does that (this
     * bus is shared with MP2731/MAX17303, confirmed via peripherals.c).
     * Re-initializing it here would just redundantly reconfigure the
     * same bus peripherals.c already set up -- same class of fix
     * already applied to the HAL-adapter GPIO pins elsewhere in this
     * port. Only the non-blocking transfer handle + callback are set
     * up here, matching Init_I2C()'s LPI2C_MasterTransferCreateHandle()
     * call. */
    LPI2C_MasterTransferCreateHandle(LPI2C1_PERIPHERAL, &s_handle, lpi2c1_bus_callback, NULL);
}

int lpi2c1_bus_transfer(uint8_t slave_address, int direction, uint8_t subaddress,
                         uint8_t *data, size_t data_size)
{
    status_t st;

    /* Was the masterXfer setup block, identical in shape to both
     * I2C_function_write() and I2C_function_read() in your I2C.c. */
    s_xfer.slaveAddress = slave_address; /* 7-bit address, not 8-bit -- confirmed */
    s_xfer.direction = direction ? kLPI2C_Read : kLPI2C_Write;
    s_xfer.subaddress = (uint32_t)subaddress;
    s_xfer.subaddressSize = 1;
    s_xfer.data = data;
    s_xfer.dataSize = data_size;
    s_xfer.flags = kLPI2C_TransferDefaultFlag;

    st = LPI2C_MasterTransferNonBlocking(LPI2C1_PERIPHERAL, &s_handle, &s_xfer);
    if (st != kStatus_Success) {
        return -1;
    }

    /* Was `while ((!g_MasterCompletionFlag) && (!g_MasterNackFlag)) {}` --
     * identical spin-wait, same caveat as any busy-wait in this port:
     * blocks this call's caller until the transfer resolves (I2C
     * transactions are short, so this is consistent with every other
     * I2C driver call in this port already being a blocking-from-the-
     * caller's-perspective operation, just via a different underlying
     * SDK mechanism now). */
    while (!s_completion_flag && !s_nack_flag) {
        /* wait */
    }

    if (s_nack_flag) {
        s_nack_flag = false;
        return -1;
    }
    s_completion_flag = false;
    return 0;
}
