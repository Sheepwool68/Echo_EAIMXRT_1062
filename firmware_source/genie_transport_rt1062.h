/*
 * genie_transport_rt1062.h
 *
 * ==========================================================================
 * SCAFFOLD -- not compiled/tested against real hardware here, same tier as
 * uhf_transport_rt1062.c/gprs_transport_rt1062.c. Was open_4D()'s
 * serCopen(115200) -- the 4D display's serial link, confirmed 115200 baud
 * from the pasted source.
 * ==========================================================================
 */

#ifndef GENIE_TRANSPORT_RT1062_H
#define GENIE_TRANSPORT_RT1062_H

#include "genie_display.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GENIE_UART_BAUDRATE 115200u

genie_transport_t genie_transport_rt1062_init(void);

#ifdef __cplusplus
}
#endif

#endif /* GENIE_TRANSPORT_RT1062_H */
