#ifndef UHF_TRANSPORT_RT1062_H
#define UHF_TRANSPORT_RT1062_H

#include "uhf_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initializes LPUART for the UHF reader link and returns a populated
 * transport. See uhf_transport_rt1062.c header comment -- fill in the
 * LPUART instance/pins for your board before use.
 */
uhf_transport_t uhf_transport_rt1062_init(void);

#ifdef __cplusplus
}
#endif

#endif /* UHF_TRANSPORT_RT1062_H */
