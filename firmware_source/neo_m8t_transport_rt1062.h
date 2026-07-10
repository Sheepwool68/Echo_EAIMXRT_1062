/*
 * neo_m8t_transport_rt1062.h
 *
 * ==========================================================================
 * SCAFFOLD -- checked by eye against the standard SDK LPSPI/GPIO/interrupt
 * APIs only, same tier and caveats as nrf_spi_transport_rt1062.c: confirm the
 * exact SDK version's API, and fill in the real LPSPI instance / CS pin /
 * PPS interrupt pin for your schematic before trusting this on hardware.
 * ==========================================================================
 *
 * Was NEOM8T.LIB's header comment: "PC0=MOSI, PC1=MISO, PE3=CLK, PB5=CS"
 * (Rabbit pins) -- translated to an RT1062 LPSPI instance + manual GPIO CS,
 * same pattern as nrf_spi_transport_rt1062.c. Also implements my_isr(), the
 * PPS GPIO interrupt handler.
 */

#ifndef NEO_M8T_TRANSPORT_RT1062_H
#define NEO_M8T_TRANSPORT_RT1062_H

#include "neo_m8t_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

neo_m8t_transport_t neo_m8t_transport_rt1062_init(void);

uint32_t neo_m8t_gps_get_tick_count(void);

int neo_m8t_gps_get_pps_state(void);

#ifdef __cplusplus
}
#endif

#endif /* NEO_M8T_TRANSPORT_RT1062_H */
