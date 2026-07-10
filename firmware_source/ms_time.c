#include "ms_time.h"

uint32_t ms_elapsed(uint32_t now, uint32_t since)
{
    return now - since;
}

int ms_has_elapsed(uint32_t now, uint32_t since, uint32_t duration)
{
    return ms_elapsed(now, since) >= duration;
}
