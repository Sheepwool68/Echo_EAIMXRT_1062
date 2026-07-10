#ifndef GPRS_TRANSPORT_RT1062_H
#define GPRS_TRANSPORT_RT1062_H

#include "gprs_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initializes LPUART + the wake/power-enable GPIOs for the 4G modem
 * link and returns a populated transport. See
 * gprs_transport_rt1062.c header comment -- fill in the LPUART
 * instance and GPIO pins for your board before use.
 */
gprs_transport_t gprs_transport_rt1062_init(void);

#ifdef __cplusplus
}
#endif

#endif /* GPRS_TRANSPORT_RT1062_H */
