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
#include "systick_ms_rt1062.h"
#include "debug_console_rt1062.h"
#include <stdbool.h>

/* PRINTF redirect to LPUART5 -- see debug_console_rt1062.h and
 * hello_world.c's own redirect comment for the full "why" (semihosting
 * PRINTF blocks forever if the SWD/LPC-Link debug connection drops).
 * Must come after whatever transitively pulled in fsl_debug_console.h's
 * own #define PRINTF (peripherals.h, above) so this wins;
 * lpuart5_console_rt1062_init() must have already run in hello_world.c's
 * main() before any of this file's PRINTF calls fire. */
#undef PRINTF
#define PRINTF debug_printf

/* Bus hang guard: if wiring/pull-ups/IRQ routing are wrong, the callback
 * may never fire at all (neither success nor NAK) -- without this bound
 * the spin-wait below would hang the whole application forever on the
 * very first transfer attempted. 10ms is generous for a 100kHz, <=7-byte
 * transfer. */
#define LPI2C1_BUS_TIMEOUT_MS  10u

static lpi2c_master_handle_t s_handle;
static volatile bool s_completion_flag;
static volatile bool s_nack_flag;
static volatile bool s_error_flag;
static lpi2c_master_transfer_t s_xfer;

/* Was lpi2c_master_callback() in your I2C.c -- logic now split three ways
 * instead of two: NAK, genuine success, and any other status (bus error,
 * arbitration lost, etc.) are no longer lumped together. Previously any
 * non-NAK status set the same "completed" flag success did, so a real
 * transfer error was silently reported to the caller as a successful
 * transfer with whatever was left in the data buffer. */
static void lpi2c1_bus_callback(LPI2C_Type *base, lpi2c_master_handle_t *handle,
                                 status_t status, void *userData)
{
    (void)base;
    (void)handle;
    (void)userData;

    if (status == kStatus_LPI2C_Nak) {
        s_nack_flag = true;
    } else if (status == kStatus_Success) {
        s_completion_flag = true;
    } else {
        /* Was `PRINTF("Error occured during transfer!")` -- no debug
         * console wired up at this layer of this port; the caller now
         * gets a -1 return for this case via s_error_flag below instead
         * of being told the transfer succeeded. */
        s_error_flag = true;
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
     * same spin-wait shape, same caveat as any busy-wait in this port:
     * blocks this call's caller until the transfer resolves (I2C
     * transactions are short, so this is consistent with every other
     * I2C driver call in this port already being a blocking-from-the-
     * caller's-perspective operation, just via a different underlying
     * SDK mechanism now). Bounded by LPI2C1_BUS_TIMEOUT_MS now, unlike
     * the original -- see the callback comment above on why an unbounded
     * wait is unsafe here. */
    {
        uint32_t start_ms = systick_ms_now();
        while (!s_completion_flag && !s_nack_flag && !s_error_flag) {
            if ((systick_ms_now() - start_ms) >= LPI2C1_BUS_TIMEOUT_MS) {
                /* Standard NXP SDK call to stop the still-in-flight
                 * transfer and force any pending callback to resolve
                 * now, before we give up -- otherwise the callback could
                 * fire later (after this function has already returned)
                 * and leave a stale flag set for the *next* transfer to
                 * misread as its own immediate completion. */
                LPI2C_MasterTransferAbort(LPI2C1_PERIPHERAL, &s_handle);
                s_completion_flag = false;
                s_nack_flag = false;
                s_error_flag = false;
                return -1;
            }
        }
    }

    if (s_nack_flag) {
        s_nack_flag = false;
        return -1;
    }
    if (s_error_flag) {
        s_error_flag = false;
        return -1;
    }
    s_completion_flag = false;
    return 0;
}
