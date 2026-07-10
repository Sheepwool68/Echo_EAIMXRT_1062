/*
 * outreach.h
 *
 * Was ConnectToSocketServer() -- REBUILT as a genuine non-blocking state
 * machine after being told the original is a Dynamic C "cofunc" (a
 * cooperative coroutine that yields every tick rather than blocking the
 * caller). An earlier version of this port implemented it as a single
 * blocking function with the same bounded per-command timeouts as the
 * original (up to ~16s worst case on the modem path) -- that was flagged
 * at the time as a real compromise, not equivalent to yield semantics,
 * and is now replaced with this design:
 *
 *   outreach_begin(app)         -- call once to START an attempt
 *                                           (matches the cofunc first
 *                                           being invoked). Does the
 *                                           original's synchronous
 *                                           up-front work (RemoteType
 *                                           dispatch, DNS-free IP parse
 *                                           for LAN) and either finishes
 *                                           immediately or sends the
 *                                           first AT command / starts the
 *                                           TCP connect and returns.
 *
 *   outreach_step(app, now_ms)  -- call once EVERY main loop
 *                                           iteration while
 *                                           outreach_in_progress()
 *                                           is true (matches one "tick" of
 *                                           the cofunc's yield cycle: does
 *                                           a small bounded check -- has
 *                                           the current AT response
 *                                           arrived, or has this step's
 *                                           timeout elapsed -- and
 *                                           returns immediately either
 *                                           way, never blocking).
 *
 *   outreach_in_progress(app)   -- true if a connect attempt is
 *                                           currently mid-flight (not
 *                                           idle). Callers should keep
 *                                           calling _step() every
 *                                           iteration while this is true,
 *                                           REGARDLESS of any outer
 *                                           periodic retry/rate-limit
 *                                           gate -- only the DECISION to
 *                                           START a new attempt should be
 *                                           rate-limited (matching the
 *                                           original's own retry cadence
 *                                           via Settings/gprs_wait_time),
 *                                           not the progress of an
 *                                           attempt already under way.
 *
 * See app_loop.c's process_remote()/remote_main() for how these three
 * are wired together.
 *
 * FLAGGED, not invented: LANSERVER_CONN_TIMEOUT was referenced but not
 * defined in what was pasted -- LANSERVER_CONN_TIMEOUT_MS below is a
 * reasonable default (10 seconds), not a confirmed original value.
 */

#ifndef OUTREACH_H
#define OUTREACH_H

#include "app_context.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LANSERVER_CONN_TIMEOUT_MS 10000u

typedef enum {
    OUTREACH_IDLE = 0,
    /* modem path -- each *_SENT state means "command sent, waiting for
     * its response or this step's timeout" */
    OUTREACH_MODEM_CSQ_SENT,
    OUTREACH_MODEM_QICSGP_SENT,
    OUTREACH_MODEM_QIACT_SENT,
    OUTREACH_MODEM_QIOPEN_SENT,
    OUTREACH_MODEM_CLEANUP_QICLOSE_SENT,
    OUTREACH_MODEM_CLEANUP_QIDEACT_SENT,
    /* LAN path */
    OUTREACH_LAN_CONNECTING
} outreach_state_t;

/* Starts a new connection attempt. Only call this when
 * outreach_in_progress() is false (i.e. IDLE) -- matches the
 * original only ever being invoked when GPRS_STATE==NOGPRS. */
void outreach_begin(app_context_t *app, uint32_t now_ms);

/* Advances the state machine by one step. Call every main loop
 * iteration while outreach_in_progress() is true. Returns 1
 * the one call where a connection newly becomes established this
 * step, 0 otherwise (still in progress, or just gave up/failed --
 * check app->modem.gprs_state == GPRS_STATE_CONNECTED separately if
 * you need to distinguish those). */
int outreach_step(app_context_t *app, uint32_t now_ms);

/* True if a connection attempt is currently mid-flight (state != IDLE). */
int outreach_in_progress(const app_context_t *app);

#ifdef __cplusplus
}
#endif

#endif /* OUTREACH_H */
