#ifndef NRF_SPI_TRANSPORT_RT1062_H
#define NRF_SPI_TRANSPORT_RT1062_H

#include "nrf_spi_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initializes LPSPI + the CS/ready-line GPIOs for the nRF52833 link
 * and returns a populated transport ready to pass to the
 * nrf_spi_protocol.h functions.
 *
 * See nrf_spi_transport_rt1062.c header comment -- this is a scaffold
 * that needs board-specific pin values and pin muxing filled in before
 * use.
 */
nrf_spi_transport_t nrf_spi_transport_rt1062_init(void);

/*
 * Exposes this file's own confirmed-working per-transfer mode switch
 * (see rt1062_transfer()) for the GPS bus-priming read in app_init.c,
 * per explicit instruction to run that 20-byte read at Mode 1 instead
 * of the bus's permanent Mode 0 default. Same LPSPI3 base, same
 * TCR-register mechanism -- caller must switch back to Mode 0 after,
 * same as rt1062_transfer() does internally.
 */
void nrf_spi_transport_switch_bus_mode1(void);
void nrf_spi_transport_switch_bus_mode0(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF_SPI_TRANSPORT_RT1062_H */
