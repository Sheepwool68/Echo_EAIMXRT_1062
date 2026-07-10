/*
 * gprs_modem.h
 *
 * Orchestration layer tying gprs_transport.h to the modem power/AT
 * command sequencing -- was Toggle_Modem(), Remote_Disconnect(),
 * Remote_SetError(), and the serial-I/O half of GPRS_ReadSer() (the
 * CSQ/response classification itself lives in gprs_response_parser.h,
 * already pure and tested).
 *
 * Same honest scope note as uhf_reader.h: this is I/O choreography,
 * harder to meaningfully unit test than the pure protocol modules.
 * Tested here via a mock transport for command sequencing and state
 * transitions; real hardware/carrier integration testing is still
 * warranted before trusting the power-sequencing timings.
 */

#ifndef GPRS_MODEM_H
#define GPRS_MODEM_H

#include "gprs_transport.h"
#include "gprs_response_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPRS_STATE_CHANGESOCKET = 1,
    GPRS_STATE_CONNECTED = 4,
    GPRS_STATE_NOGPRS = 7,
    GPRS_STATE_MUSTRESTARTMODEM = 8,
    GPRS_STATE_WAITFORMODEMOFF = 9,
    GPRS_STATE_WAITFORMODEMRESTART = 10,
    GPRS_STATE_DISCONNECTED = 11,
} gprs_state_t;

typedef enum {
    GPRS_STATUS_OFF = 1,
    GPRS_STATUS_CON = 3,
    GPRS_STATUS_SOC = 4,
    GPRS_STATUS_ER1 = 7,
    GPRS_STATUS_ER2 = 8,
    GPRS_STATUS_ER3 = 10,
    GPRS_STATUS_ER4 = 11,
    GPRS_STATUS_MODEM_PRESENT = 12,
} gprs_status_t;

typedef struct {
    const gprs_transport_t *transport;
    int gprs_state;
    int gprs_status;
} gprs_modem_t;

void gprs_modem_init(gprs_modem_t *m, const gprs_transport_t *t);

/*
 * Was Toggle_Modem(). remote_enabled corresponds to Settings.RemoteType
 * != 0 -- nonzero wakes and prepares the modem for AT commands, zero
 * disconnects (if connected) and puts it to sleep/powers it down.
 * Calls gprs_modem_disconnect() internally when putting the modem to
 * sleep while GPRS_STATE_CONNECTED, matching the original's call to
 * Remote_Disconnect() in that branch.
 */
void gprs_modem_toggle(gprs_modem_t *m, int remote_enabled);

/*
 * Was Remote_Disconnect(). remote_type: 0 or 1 sends the Hayes '+++'
 * escape sequence (see the flagged guard-time concern in the module's
 * porting notes); 2 is a no-op here since closing the LAN socket is
 * the TCP module's responsibility (tcp_session.h), not this one's --
 * pass 2 and this function just updates gprs_state.
 */
void gprs_modem_disconnect(gprs_modem_t *m, int remote_type);

/*
 * Was Remote_SetError(). Updates gprs_state/gprs_status, and for
 * remote_type==1 calls gprs_modem_disconnect() and powers the modem
 * off (matching the original's power-cycle-on-error behavior).
 * Returns the value the caller should assign to Settings.GPRS_CurrentRec
 * (was `Settings.GPRS_CurrentRec = iGPRSBaseRecord;`) -- just
 * gprs_base_record itself, named for symmetry/clarity with the
 * original's variable.
 */
uint32_t gprs_modem_set_error(gprs_modem_t *m, int remote_type, uint32_t gprs_base_record);

/*
 * Reads a response from the modem (blocking up to timeout_ms) into
 * buf (NUL-terminated on return, matching what gprs_classify_response/
 * gprs_parse_csq expect), and classifies it against `expected`. Was
 * the serial-I/O half of GPRS_ReadSer() -- call gprs_parse_csq()
 * separately on the same buf if you were checking for "CSQ:"
 * specifically (that split lets this function stay a generic
 * read+classify primitive rather than special-casing CSQ internally
 * the way the original did).
 */
gprs_response_result_t gprs_modem_read_response(gprs_modem_t *m, const char *expected,
                                                 char *buf, size_t buf_size,
                                                 uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* GPRS_MODEM_H */
