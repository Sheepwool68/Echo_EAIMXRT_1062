#include "systick_ms_rt1062.h"
#include "fsl_common.h"

static volatile uint32_t s_ms_counter;

void SysTick_Handler(void)
{
    s_ms_counter++;
}

int systick_ms_init(void)
{
    if (SysTick_Config(SystemCoreClock / 1000u) != 0) {
        return -1;
    }
    s_ms_counter = 0;
    return 0;
}

uint32_t systick_ms_now(void)
{
    return s_ms_counter;
}

void systick_ms_delay(uint32_t ms)
{
    uint32_t start = s_ms_counter;
    while ((s_ms_counter - start) < ms) {
        /* busy wait */
    }
}
