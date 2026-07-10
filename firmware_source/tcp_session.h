/*
 * tcp_session.h
 *
 * Portable re-implementation of the per-client TCP connection
 * lifecycle from ActiveRFID.C's ProcessDataSocket(), plus the reset
 * socket check and the periodic status broadcast.
 *
 * Driven entirely through the small transport vtables below, so it's
 * unit-testable with a mock and reusable regardless of which TCP stack
 * (lwIP sockets, lwIP raw API, BSD sockets) sits underneath.
 *
 * TOPOLOGY NOTE: the original Rabbit firmware ran 3 independent
 * listen-sockets on the same port (DATA_PORT), each capturing one
 * client. lwIP doesn't support that pattern the same way; the
 * idiomatic replacement is ONE listen socket with a backlog of 3,
 * then accept() filling 3 session slots. That accept-loop lives in
 * the lwIP-specific glue (tcp_transport_lwip.c), not here -- this
 * module only cares about a single already-connected-or-listening
 * socket's lifecycle, and works the same either way.
 *
 * DESIGN NOTE: the original's periodic "V=%d\n" status broadcast
 * (WriteStatusMessages) uses ONE GLOBAL timer shared across all
 * connected sockets, so every connected client receives status at the
 * same synchronized cadence. This port preserves that by keeping the
 * broadcast as a separate function operating over an array of
 * sessions/transports, rather than folding it into each session's own
 * per-call processing (which would let each session's timer drift
 * independently -- a behavioural change from the original that we
 * deliberately avoided).
 */

#ifndef TCP_SESSION_H
#define TCP_SESSION_H

#include <stdint.h>
#include <stddef.h>
#include "pc_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;

    /* was sock_alive() != 0 */
    int (*is_alive)(void *ctx);
    /* was sock_established() */
    int (*is_established)(void *ctx);
    /*
     * Non-blocking read. Returns:
     *   > 0 : that many bytes were read into buf
     *   0   : no data currently available (not an error)
     *   < 0 : socket error
     */
    int (*recv)(void *ctx, uint8_t *buf, size_t max_len);
    /* Returns bytes written, or < 0 on error. */
    int (*send)(void *ctx, const uint8_t *buf, size_t len);
    /* Re-arms listening after a disconnect (was TCPIPOpen()) */
    void (*reopen_listen)(void *ctx);
    /* Disables Nagle / sets up keepalive the way the original did
     * once per new connection (was tcp_keepalive(...,0)) */
    void (*on_new_connection)(void *ctx);
} tcp_socket_transport_t;

typedef struct {
    int client_connected; /* was MyServerSocket[i].ClientIsConnected */
} tcp_socket_session_t;

typedef enum {
    TCP_SESSION_IDLE = 0,        /* nothing new this call */
    TCP_SESSION_NEWLY_CONNECTED, /* greeting + battery status just sent */
    TCP_SESSION_DISCONNECTED,    /* client dropped since last call */
    TCP_SESSION_COMMAND,         /* a command was received; *out_cmd is populated */
} tcp_session_event_t;

/*
 * Call once per main-loop iteration per socket slot. Mirrors
 * ProcessDataSocket() minus the periodic status broadcast (see
 * tcp_broadcast_status below) and minus RemoteConfigRec handling
 * (Outreach/GPRS-specific; not present in the reviewed source -- port
 * separately once you have that library's definitions).
 */
tcp_session_event_t tcp_session_process(const tcp_socket_transport_t *t,
                                         tcp_socket_session_t *session,
                                         unsigned long last_time_sent,
                                         int batt_percent,
                                         pc_parsed_command_t *out_cmd);

/*
 * Broadcasts "V=<percent>\n" to every currently-connected session in
 * the array, at most once per interval_ms (was the hardcoded 2000ms /
 * iLastStatusTime check in WriteStatusMessages).
 *
 * *last_broadcast_ms is caller-owned state (like the original's global
 * iLastStatusTime) -- pass the same variable across calls.
 *
 * Returns the number of sockets a status line was actually sent to
 * (0 if the interval hasn't elapsed yet, or no sockets connected).
 */
int tcp_broadcast_status(const tcp_socket_transport_t *transports,
                          const tcp_socket_session_t *sessions,
                          size_t count,
                          int batt_percent,
                          uint32_t now_ms,
                          uint32_t interval_ms,
                          uint32_t *last_broadcast_ms);

/*
 * Reset-socket check (was ProcessResetSocket's TCP half). Returns 1 if
 * a client has connected to the reset socket -- caller should then
 * close and reopen all data sockets, matching TCPIPCloseSockets() +
 * TCPIPOpenSockets() in the original, and should close/reopen the
 * reset socket's transport itself too.
 */
int tcp_reset_triggered(const tcp_socket_transport_t *reset_transport);

#ifdef __cplusplus
}
#endif

#endif /* TCP_SESSION_H */
