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

#ifdef __cplusplus
}
#endif

#endif /* NRF_SPI_TRANSPORT_RT1062_H */
