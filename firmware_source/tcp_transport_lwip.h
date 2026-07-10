/*
 * tcp_transport_lwip.h
 *
 * REVISED: bare-metal (NO_SYS=1), lwIP RAW/callback API -- not the
 * sockets API. See tcp_transport_lwip.c's header comment for the raw
 * API gotchas this implementation has to get right.
 *
 * The public shape is UNCHANGED from the sockets version: still
 * populates the same tcp_socket_transport_t/udp_transport_t interfaces
 * from tcp_session.h/udp_discovery.h, because those were already
 * poll-style (recv() returns 0 for "nothing yet" rather than
 * blocking) -- nothing in tcp_session.c, pc_protocol.c, or app_loop.c
 * needs to change for this swap.
 *
 * NEW REQUIREMENT this introduces: call tcp_lwip_poll() once per main
 * loop iteration. This drives lwIP's timers (sys_check_timeouts()) and
 * is the single consolidated replacement for the many scattered
 * tcp_tick(NULL) calls in the original.
 */

#ifndef TCP_TRANSPORT_LWIP_H
#define TCP_TRANSPORT_LWIP_H

#include <stdint.h>
#include "tcp_session.h"
#include "udp_discovery.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TCP_LWIP_MAX_CLIENTS 3

/* Per-connection state: a raw tcp_pcb plus the ring buffer that
 * bridges the asynchronous recv callback into tcp_session.h's
 * synchronous poll-style recv(). pcb is void* here to avoid pulling
 * lwip/tcp.h into this header -- only tcp_transport_lwip.c casts it. */
typedef struct {
    void *pcb;
    uint8_t rx_ring[512];
    volatile uint16_t rx_head, rx_tail;
    volatile int established;
    volatile int alive;
} tcp_lwip_conn_t;

typedef struct {
    void *listen_pcb;
    tcp_lwip_conn_t conns[TCP_LWIP_MAX_CLIENTS];
    tcp_socket_transport_t transports[TCP_LWIP_MAX_CLIENTS];
    tcp_socket_session_t sessions[TCP_LWIP_MAX_CLIENTS];
} tcp_lwip_listener_t;

/* Opens the listen pcb on `port` with a backlog of TCP_LWIP_MAX_CLIENTS. */
int tcp_lwip_listener_open(tcp_lwip_listener_t *listener, uint16_t port);

/*
 * Call once per main-loop iteration: performs any per-slot cleanup
 * that couldn't happen synchronously inside a callback (e.g. closing
 * a pcb whose remote end closed on us). Unlike the sockets version,
 * accept() itself is NOT polled here -- raw API's accept callback
 * fires on its own whenever a connection arrives; this function only
 * handles slot bookkeeping.
 */
void tcp_lwip_listener_poll_accept(tcp_lwip_listener_t *listener);

/* Reset-socket (single client, port RESET_PORT) */
int tcp_lwip_reset_socket_open(tcp_socket_transport_t *out_transport, uint16_t port);

/*
 * Was tcp_open()+sock_established() in ConnectToSocketServer()'s LAN
 * branch -- now genuinely non-blocking, two-phase (matching the
 * original cofunc's real yield-every-tick semantics rather than
 * blocking the caller). Call _start() once to initiate the connect,
 * then _poll() once per main loop iteration until it returns nonzero.
 * ip is big-endian (octet 'a' in the most significant byte -- see
 * ip_addr_parse.h). Returns 0 on successful start (not yet
 * established), -1 on immediate failure (e.g. tcp_new() failed).
 */
int tcp_lwip_client_connect_start(tcp_lwip_conn_t *conn, uint32_t ip, uint16_t port);

/*
 * Was a single tick of the blocking wait loop -- makes ONE
 * non-blocking check and returns immediately either way. Returns 1 =
 * established (out_transport filled in), 0 = still pending (call
 * again later), -1 = failed/timed out.
 */
int tcp_lwip_client_connect_poll(tcp_lwip_conn_t *conn, tcp_socket_transport_t *out_transport,
                                  uint32_t start_ms, uint32_t now_ms, uint32_t timeout_ms);

/* UDP discovery responder socket (port 2000 in the original) */
int tcp_lwip_udp_discovery_open(udp_transport_t *out_transport, uint16_t port);

/*
 * Call once per main loop iteration, unconditionally. Drives lwIP's
 * internal timers (ARP, TCP retransmit, etc) via sys_check_timeouts().
 * Does NOT drive Ethernet RX itself -- that's your board's ENET
 * driver's job (typically MCUXpresso's generated ethernetif.c calling
 * netif->input() from either an RX ISR or its own poll function);
 * call that separately, before or after this, per your driver's docs.
 */
void tcp_lwip_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* TCP_TRANSPORT_LWIP_H */
