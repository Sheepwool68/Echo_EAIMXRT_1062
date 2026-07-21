/*
 * lpuart5_console_rt1062.c
 *
 * NEW CODE -- see header comment. Same interrupt-driven RX ring buffer +
 * blocking TX shape as gprs_transport_rt1062.c's LPUART1 driver, for
 * consistency with the rest of this port.
 */

#include "lpuart5_console_rt1062.h"
#include "fsl_lpuart.h"
#include "fsl_common.h"
#include "fsl_clock.h"
#include "pin_mux.h" /* confirmed real: TX on GPIO_B1_12, RX on GPIO_B1_13 */

/* CONFIRMED from board/clock_config.c: UART_CLK_ROOT.outFreq = 80MHz,
 * shared by all LPUART instances (single kCLOCK_UartMux/kCLOCK_UartDiv,
 * not per-instance) -- same clock LPUART1 uses. No LPUART5_PERIPHERAL
 * macro exists (never added to Config Tools' peripheral group), so this
 * uses the raw CMSIS LPUART5 symbol directly. */
#define LPUART5_CLK_FREQ_HZ 80000000UL

#define LPUART5_RX_RING_SIZE 256u

static volatile uint8_t s_rx_ring[LPUART5_RX_RING_SIZE];
static volatile size_t s_rx_head;
static volatile size_t s_rx_tail;

/* Overrun handling added 2026-07-17 -- see uhf_transport_rt1062.c's
 * LPUART8_IRQHandler() for the full story (found there first, this
 * driver shares the identical gap): without checking/clearing
 * kLPUART_RxOverrunFlag, an overrun permanently deadlocks RDRF (and
 * therefore this whole ISR) since RDRF can't assert again while OR is
 * held -- nothing left to ever re-enter this handler and clear it. */
void LPUART5_IRQHandler(void)
{
    if (LPUART_GetStatusFlags(LPUART5) & kLPUART_RxOverrunFlag) {
        LPUART_ClearStatusFlags(LPUART5, kLPUART_RxOverrunFlag);
    }

    while (LPUART_GetStatusFlags(LPUART5) & kLPUART_RxDataRegFullFlag) {
        uint8_t byte = LPUART_ReadByte(LPUART5);
        size_t next_head = (s_rx_head + 1) % LPUART5_RX_RING_SIZE;
        if (next_head != s_rx_tail) {
            s_rx_ring[s_rx_head] = byte;
            s_rx_head = next_head;
        }
    }
    SDK_ISR_EXIT_BARRIER;
}

void lpuart5_console_rt1062_init(void)
{
    lpuart_config_t config;

    LPUART_GetDefaultConfig(&config);
    config.baudRate_Bps = 115200u;
    config.enableTx = true;
    config.enableRx = true;

    LPUART_Init(LPUART5, &config, LPUART5_CLK_FREQ_HZ);

    s_rx_head = 0;
    s_rx_tail = 0;

    /* kLPUART_RxOverrunInterruptEnable added 2026-07-17 alongside the
     * ISR fix above -- without it, an overrun could never re-trigger
     * this ISR to clear itself (RDRF, the only previously-enabled
     * source, is exactly what an unaddressed overrun suppresses). */
    LPUART_EnableInterrupts(LPUART5,
                             kLPUART_RxDataRegFullInterruptEnable | kLPUART_RxOverrunInterruptEnable);
    EnableIRQ(LPUART5_IRQn);
}

void lpuart5_console_write(const uint8_t *buf, size_t len)
{
    LPUART_WriteBlocking(LPUART5, buf, len);
}

size_t lpuart5_console_read(uint8_t *buf, size_t max_len)
{
    size_t n = 0;
    while (n < max_len && s_rx_tail != s_rx_head) {
        buf[n] = s_rx_ring[s_rx_tail];
        s_rx_tail = (s_rx_tail + 1) % LPUART5_RX_RING_SIZE;
        n++;
    }
    return n;
}
