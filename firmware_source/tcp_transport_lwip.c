/*
 * tcp_transport_lwip.c
 *
 * ==========================================================================
 * SCAFFOLD -- NOT COMPILED OR TESTED IN THIS ENVIRONMENT (no lwIP here),
 * same tier as the other hardware/stack glue in this port. REWRITTEN from
 * the earlier sockets-API version to lwIP's RAW/callback API for true
 * bare-metal (NO_SYS=1) operation -- no RTOS, no FreeRTOS, no blocking
 * calls anywhere in this file.
 *
 * RAW API CORRECTNESS NOTES (get these wrong and connections silently
 * misbehave rather than erroring cleanly):
 *   1. After tcp_err()'s callback fires, the pcb is ALREADY freed by lwIP.
 *      Never call any tcp_* function on it again -- just clear our own
 *      bookkeeping (conn->pcb = NULL).
 *   2. tcp_recv() callback with pbuf==NULL means the remote closed the
 *      connection -- WE must call tcp_close() ourselves in response; lwIP
 *      doesn't do it automatically.
 *   3. Received data must be acknowledged via tcp_recved() once consumed
 *      into our ring buffer, or the far end's advertised window stops
 *      growing and the connection stalls under load.
 *   4. tcp_write()/tcp_output() can be called anytime (not just from
 *      inside a callback), but must respect tcp_sndbuf()'s current limit --
 *      exceeding it silently fails rather than blocking (there's no thread
 *      to block on).
 *
 * VERSION-DEPENDENT: the IPv4-address helper calls
 * (ip4_addr_get_u32/ip_2_ip4/ip_addr_set_ip4_u32) vary slightly across
 * lwIP versions/configurations (IPv4-only vs dual-stack builds use
 * different accessor macros). Verify against your project's actual lwIP
 * version before trusting this as-is.
 * ==========================================================================
 */

#include "tcp_transport_lwip.h"

#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "lwip/ip_addr.h"
#include "systick_ms_rt1062.h"
#include "ms_time.h"
#include "debug_console_rt1062.h"
#include <string.h>

/* PRINTF redirect to LPUART5 -- added 2026-07-21 for the lw_send()
 * window tracing below (TEMPORARY DIAGNOSTIC), see that call site's own
 * comment. */
#undef PRINTF
#define PRINTF debug_printf

#define RX_RING_SIZE 512 /* must match tcp_lwip_conn_t.rx_ring's declared size */

/* ------------------------------------------------------------------ */
/* Ring buffer helper (bridges async recv callback -> sync poll recv()) */
/* ------------------------------------------------------------------ */

static uint16_t ring_push(tcp_lwip_conn_t *c, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        uint16_t next = (uint16_t)((c->rx_head + 1) % RX_RING_SIZE);
        if (next == c->rx_tail) {
            /* Ring full -- drop the remainder. Shouldn't happen if the
             * main loop drains recv() every iteration; flagged rather
             * than silently growing a buffer we don't have RAM for.
             * Returns how many bytes actually made it in (was void --
             * changed 2026-07-20, see conn_recv_cb()'s own comment for
             * why the caller needs this count). */
            break;
        }
        c->rx_ring[c->rx_head] = data[i];
        c->rx_head = next;
    }
    return i;
}

/* ------------------------------------------------------------------ */
/* Per-connection TCP callbacks                                         */
/* ------------------------------------------------------------------ */

static err_t conn_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    tcp_lwip_conn_t *c = (tcp_lwip_conn_t *)arg;
    (void)err;

    if (p == NULL) {
        /* Remote closed -- WE must close our side (note 2 above). */
        c->alive = 0;
        c->established = 0;
        tcp_close(tpcb);
        c->pcb = NULL;
        return ERR_OK;
    }

    {
        /* FIXED 2026-07-20, found via review: was unconditionally
         * calling tcp_recved(tpcb, p->tot_len) below regardless of
         * whether ring_push() actually stored all of it. If the ring
         * was full and silently dropped some bytes (its own documented,
         * pre-existing behavior), telling lwIP "fully consumed, grow
         * the window back up" for bytes that were actually just thrown
         * away meant no backpressure ever reached the sender -- the
         * SAME overflow could keep recurring under any sustained burst
         * exceeding what the main loop drains per iteration, silently
         * corrupting whatever PC command/data stream was mid-flight,
         * with nothing anywhere indicating data was lost. Now only acks
         * what was actually buffered; the sender's advertised window
         * shrinks accordingly, giving real TCP-level backpressure until
         * the main loop drains the ring via recv(). The dropped bytes
         * from THIS burst are still gone (nothing buffers them
         * separately for a retry) -- this fixes the false "all good"
         * signal, not retroactive recovery of already-discarded data. */
        struct pbuf *q;
        uint16_t consumed = 0;
        for (q = p; q != NULL; q = q->next) {
            consumed = (uint16_t)(consumed + ring_push(c, (const uint8_t *)q->payload, q->len));
        }
        tcp_recved(tpcb, consumed); /* note 3 above */
    }
    pbuf_free(p);
    return ERR_OK;
}

static void conn_err_cb(void *arg, err_t err)
{
    tcp_lwip_conn_t *c = (tcp_lwip_conn_t *)arg;
    (void)err;
    /* pcb is ALREADY freed here (note 1 above) -- just clear bookkeeping. */
    c->pcb = NULL;
    c->alive = 0;
    c->established = 0;
}

static err_t conn_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_lwip_listener_t *listener = (tcp_lwip_listener_t *)arg;
    int i;
    (void)err;

    /* REQUIRED whenever TCP_LISTEN_BACKLOG is enabled (it is -- see
     * lwipopts.h, "Enable backlog", TCP_LISTEN_BACKLOG=1) -- added
     * 2026-07-20, found via review (not a live-hardware symptom yet):
     * lwIP tracks a pending-connection count against the LISTENING
     * pcb's backlog limit, incremented internally when a SYN arrives.
     * Without an explicit tcp_accepted() call here, that count is
     * never decremented -- once enough connect/disconnect cycles have
     * happened to exhaust the backlog (TCP_LWIP_MAX_CLIENTS, passed to
     * tcp_listen_with_backlog() in tcp_lwip_listener_open()), lwIP
     * silently refuses ALL further connection attempts, even from
     * clients reconnecting after a previous one cleanly disconnected --
     * a slow degradation that would be brutal to diagnose through
     * repeated hardware reflash cycles rather than caught here. */
    tcp_accepted((struct tcp_pcb *)listener->listen_pcb);

    for (i = 0; i < TCP_LWIP_MAX_CLIENTS; i++) {
        if (!listener->conns[i].alive) {
            tcp_lwip_conn_t *c = &listener->conns[i];
            c->pcb = newpcb;
            c->rx_head = 0;
            c->rx_tail = 0;
            c->established = 1;
            c->alive = 1;

            tcp_arg(newpcb, c);
            tcp_recv(newpcb, conn_recv_cb);
            tcp_err(newpcb, conn_err_cb);
            tcp_nagle_disable(newpcb);

            return ERR_OK;
        }
    }

    /* No free slot -- matches the original's fixed 3-connection limit;
     * lwIP will handle rejecting/resetting the new connection for us. */
    return ERR_MEM;
}

/* ------------------------------------------------------------------ */
/* tcp_socket_transport_t implementation (shared by data + reset sockets) */
/* ------------------------------------------------------------------ */

static int lw_is_alive(void *ctx) { return ((tcp_lwip_conn_t *)ctx)->alive; }
static int lw_is_established(void *ctx) { return ((tcp_lwip_conn_t *)ctx)->established; }

static int lw_recv(void *ctx, uint8_t *buf, size_t max_len)
{
    tcp_lwip_conn_t *c = (tcp_lwip_conn_t *)ctx;
    size_t n = 0;
    while (n < max_len && c->rx_tail != c->rx_head) {
        buf[n++] = c->rx_ring[c->rx_tail];
        c->rx_tail = (uint16_t)((c->rx_tail + 1) % RX_RING_SIZE);
    }
    return (int)n;
}

static int lw_send(void *ctx, const uint8_t *buf, size_t len)
{
    tcp_lwip_conn_t *c = (tcp_lwip_conn_t *)ctx;
    struct tcp_pcb *pcb = (struct tcp_pcb *)c->pcb;
    err_t err;
    u16_t avail;

    if (pcb == NULL || !c->alive) {
        return -1;
    }

    avail = tcp_sndbuf(pcb);
    if (avail == 0) {
        return 0; /* send buffer currently full -- caller retries next
                      iteration; matches how a blocking write would have
                      just taken longer under the old sockets model,
                      rather than failing outright */
    }
    if (len > avail) {
        len = avail; /* partial write -- caller (tcp_session.c etc) should
                         already tolerate short writes since this mirrors
                         normal TCP send behavior anyway */
    }

    err = tcp_write(pcb, buf, (u16_t)len, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        return -1;
    }
    tcp_output(pcb);

    /* TEMPORARY DIAGNOSTIC, added 2026-07-21 per explicit report of
     * "no data on the client" despite every send() call site reporting
     * full success. tcp_sndbuf() (avail, above) only reflects OUR OWN
     * local buffer capacity -- it says nothing about the PEER's
     * currently advertised receive window (pcb->snd_wnd), which is what
     * actually gates whether lwIP transmits new data onto the wire.
     * tcp_write()/tcp_output() can both report success while lwIP
     * silently withholds the segment because the peer's window is zero
     * or too small -- a normal TCP flow-control mechanism, not a bug in
     * this code, but invisible at every layer traced so far since none
     * of them look at the peer's window. Logging it directly here to
     * either confirm or rule this out. Remove once this report is
     * resolved. */
    PRINTF("lw_send: len=%u snd_wnd=%u snd_buf=%u unacked=%u unsent=%u\r\n",
           (unsigned)len, (unsigned)pcb->snd_wnd, (unsigned)pcb->snd_buf,
           (unsigned)(pcb->unacked != NULL), (unsigned)(pcb->unsent != NULL));

    return (int)len;
}

static void lw_reopen_listen(void *ctx)
{
    /* Under the accept-callback model, a slot frees itself once
     * alive==0; there's nothing to actively re-arm here (unlike the
     * old sockets version, which had to call accept() again). */
    (void)ctx;
}

static void lw_on_new_connection(void *ctx)
{
    /* Nagle is already disabled in conn_accept_cb; nothing further
     * needed here. Kept as a no-op rather than removed, so the
     * tcp_socket_transport_t vtable stays fully populated. */
    (void)ctx;
}

/* ------------------------------------------------------------------ */
/* Listener (3 data-socket slots)                                       */
/* ------------------------------------------------------------------ */

int tcp_lwip_listener_open(tcp_lwip_listener_t *listener, uint16_t port)
{
    struct tcp_pcb *pcb;
    int i;

    memset(listener, 0, sizeof(*listener));

    pcb = tcp_new();
    if (pcb == NULL) {
        return -1;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) {
        tcp_close(pcb);
        return -1;
    }
    pcb = tcp_listen_with_backlog(pcb, TCP_LWIP_MAX_CLIENTS);
    if (pcb == NULL) {
        return -1; /* tcp_listen_with_backlog frees the original pcb on failure */
    }

    tcp_arg(pcb, listener);
    tcp_accept(pcb, conn_accept_cb);
    listener->listen_pcb = pcb;

    for (i = 0; i < TCP_LWIP_MAX_CLIENTS; i++) {
        listener->transports[i].ctx = &listener->conns[i];
        listener->transports[i].is_alive = lw_is_alive;
        listener->transports[i].is_established = lw_is_established;
        listener->transports[i].recv = lw_recv;
        listener->transports[i].send = lw_send;
        listener->transports[i].reopen_listen = lw_reopen_listen;
        listener->transports[i].on_new_connection = lw_on_new_connection;
    }

    return 0;
}

void tcp_lwip_listener_poll_accept(tcp_lwip_listener_t *listener)
{
    int i;
    for (i = 0; i < TCP_LWIP_MAX_CLIENTS; i++) {
        tcp_lwip_conn_t *c = &listener->conns[i];
        if (!c->alive && c->pcb != NULL) {
            /* Defensive cleanup -- normally recv_cb/err_cb already clear
             * pcb when a connection dies, but close+clear here too in
             * case some path leaves it set. */
            tcp_close((struct tcp_pcb *)c->pcb);
            c->pcb = NULL;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Reset socket (single client)                                         */
/* ------------------------------------------------------------------ */

static tcp_lwip_conn_t s_reset_conn;
static struct tcp_pcb *s_reset_listen_pcb;

static err_t reset_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg; (void)err;

    /* Same requirement as conn_accept_cb() above (TCP_LISTEN_BACKLOG=1)
     * -- this listener's own pcb wasn't reachable via `arg` (tcp_arg()
     * was never called on it, only on each accepted connection), so a
     * dedicated static holds it instead, set in
     * tcp_lwip_reset_socket_open() below. */
    tcp_accepted(s_reset_listen_pcb);

    if (s_reset_conn.alive) {
        return ERR_MEM; /* already have a client -- matches the original's
                            single-reset-socket contract */
    }
    s_reset_conn.pcb = newpcb;
    s_reset_conn.established = 1;
    s_reset_conn.alive = 1;
    s_reset_conn.rx_head = 0;
    s_reset_conn.rx_tail = 0;
    tcp_arg(newpcb, &s_reset_conn);
    tcp_recv(newpcb, conn_recv_cb);
    tcp_err(newpcb, conn_err_cb);
    return ERR_OK;
}

int tcp_lwip_reset_socket_open(tcp_socket_transport_t *out_transport, uint16_t port)
{
    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) {
        return -1;
    }
    if (tcp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) {
        tcp_close(pcb);
        return -1;
    }
    pcb = tcp_listen_with_backlog(pcb, 1);
    if (pcb == NULL) {
        return -1;
    }
    tcp_accept(pcb, reset_accept_cb);
    s_reset_listen_pcb = pcb;

    memset(&s_reset_conn, 0, sizeof(s_reset_conn));

    out_transport->ctx = &s_reset_conn;
    out_transport->is_alive = lw_is_alive;
    out_transport->is_established = lw_is_established;
    out_transport->recv = lw_recv;
    out_transport->send = lw_send;
    out_transport->reopen_listen = lw_reopen_listen;
    out_transport->on_new_connection = lw_on_new_connection;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Outbound TCP client connect                                          */
/* ------------------------------------------------------------------ */

static err_t client_connected_cb(void *arg, struct tcp_pcb *tpcb, err_t err)
{
    tcp_lwip_conn_t *c = (tcp_lwip_conn_t *)arg;
    (void)tpcb;
    if (err == ERR_OK) {
        c->established = 1;
    }
    /* if err != ERR_OK, conn_err_cb (already registered below) will
     * fire separately and clear c->alive -- nothing further to do here */
    return ERR_OK;
}

/*
 * Was tcp_open() + the blocking wait-for-established loop -- now
 * genuinely non-blocking, two-phase, matching the original
 * ConnectToSocketServer() cofunc's real semantics (yields every tick
 * rather than blocking the caller). Call _start() once to initiate,
 * then _poll() once per main loop iteration until it returns nonzero.
 */
int tcp_lwip_client_connect_start(tcp_lwip_conn_t *conn, uint32_t ip, uint16_t port)
{
    struct tcp_pcb *pcb;
    ip_addr_t dst;

    memset(conn, 0, sizeof(*conn));

    pcb = tcp_new();
    if (pcb == NULL) {
        return -1;
    }

    conn->pcb = pcb;
    conn->alive = 1;
    conn->established = 0;

    tcp_arg(pcb, conn);
    tcp_recv(pcb, conn_recv_cb);
    tcp_err(pcb, conn_err_cb);

    /* TODO VERSION-DEPENDENT (see file header): same IPv4 accessor
     * caveat as the UDP section below. */
    ip_addr_set_ip4_u32(&dst, ip);

    if (tcp_connect(pcb, &dst, port, client_connected_cb) != ERR_OK) {
        conn->alive = 0;
        return -1;
    }

    return 0;
}

/*
 * Was the blocking wait loop's single iteration -- call this once per
 * main loop iteration (it does NOT loop or block internally; it makes
 * one non-blocking check and returns immediately either way, matching
 * a single "tick" of the original cofunc's yield cycle). start_ms
 * should be the timestamp tcp_lwip_client_connect_start() was called
 * at; now_ms is the current time.
 *
 * Returns: 1 = established (out_transport filled in, ready to use),
 * 0 = still pending (call again later), -1 = failed or timed out
 * (matches the original's implicit "give up, caller retries/falls
 * back" behavior on timeout -- tcp_abort() rather than tcp_close()
 * since the connection attempt itself, not an established connection,
 * is what's being abandoned).
 */
int tcp_lwip_client_connect_poll(tcp_lwip_conn_t *conn, tcp_socket_transport_t *out_transport,
                                  uint32_t start_ms, uint32_t now_ms, uint32_t timeout_ms)
{
    tcp_lwip_poll();

    if (conn->established) {
        out_transport->ctx = conn;
        out_transport->is_alive = lw_is_alive;
        out_transport->is_established = lw_is_established;
        out_transport->recv = lw_recv;
        out_transport->send = lw_send;
        out_transport->reopen_listen = lw_reopen_listen;
        out_transport->on_new_connection = lw_on_new_connection;
        return 1;
    }

    if (!conn->alive) {
        return -1; /* conn_err_cb already fired */
    }

    if (ms_elapsed(now_ms, start_ms) >= timeout_ms) {
        tcp_abort(conn->pcb);
        conn->pcb = NULL;
        conn->alive = 0;
        return -1;
    }

    return 0; /* still pending */
}

/* ------------------------------------------------------------------ */
/* UDP discovery socket                                                 */
/* ------------------------------------------------------------------ */

#define UDP_RX_QUEUE_LEN 4

typedef struct {
    uint8_t data[128];
    size_t len;
    ip_addr_t addr;
    u16_t port;
} udp_dgram_t;

static struct udp_pcb *s_udp_pcb;
static udp_dgram_t s_udp_queue[UDP_RX_QUEUE_LEN];
static volatile int s_udp_queue_head, s_udp_queue_tail;

static void udp_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port)
{
    (void)arg; (void)pcb;
    if (p != NULL) {
        int next = (s_udp_queue_head + 1) % UDP_RX_QUEUE_LEN;
        if (next != s_udp_queue_tail) {
            udp_dgram_t *d = &s_udp_queue[s_udp_queue_head];
            size_t n = p->tot_len;
            if (n > sizeof(d->data)) {
                n = sizeof(d->data);
            }
            pbuf_copy_partial(p, d->data, (u16_t)n, 0);
            d->len = n;
            d->addr = *addr;
            d->port = port;
            s_udp_queue_head = next;
        }
        /* if the queue was full, the datagram is silently dropped --
         * matches UDP's inherent unreliability, no special handling needed */
        pbuf_free(p);
    }
}

static int udp_recv_from_impl(void *ctx, uint8_t *buf, size_t max_len,
                               uint32_t *out_ip, uint16_t *out_port)
{
    (void)ctx;
    if (s_udp_queue_tail == s_udp_queue_head) {
        return 0;
    }
    {
        udp_dgram_t *d = &s_udp_queue[s_udp_queue_tail];
        size_t n = (d->len < max_len) ? d->len : max_len;
        memcpy(buf, d->data, n);
        /* TODO VERSION-DEPENDENT (see file header): confirm these IPv4
         * accessor macros against your lwIP build's actual config. */
        *out_ip = ip4_addr_get_u32(ip_2_ip4(&d->addr));
        *out_port = d->port;
        s_udp_queue_tail = (s_udp_queue_tail + 1) % UDP_RX_QUEUE_LEN;
        return (int)n;
    }
}

static int udp_send_to_impl(void *ctx, const uint8_t *buf, size_t len,
                             uint32_t ip, uint16_t port)
{
    (void)ctx;
    struct pbuf *p;
    ip_addr_t dst;
    err_t err;

    p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    if (p == NULL) {
        return -1;
    }
    memcpy(p->payload, buf, len);
    /* TODO VERSION-DEPENDENT: same caveat as udp_recv_from_impl. */
    ip_addr_set_ip4_u32(&dst, ip);

    err = udp_sendto(s_udp_pcb, p, &dst, port);
    pbuf_free(p);
    return (err == ERR_OK) ? (int)len : -1;
}

int tcp_lwip_udp_discovery_open(udp_transport_t *out_transport, uint16_t port)
{
    s_udp_pcb = udp_new();
    if (s_udp_pcb == NULL) {
        return -1;
    }
    if (udp_bind(s_udp_pcb, IP_ADDR_ANY, port) != ERR_OK) {
        return -1;
    }
    udp_recv(s_udp_pcb, udp_recv_cb, NULL);

    s_udp_queue_head = 0;
    s_udp_queue_tail = 0;

    out_transport->ctx = NULL;
    out_transport->recv_from = udp_recv_from_impl;
    out_transport->send_to = udp_send_to_impl;
    return 0;
}

/* ------------------------------------------------------------------ */

void tcp_lwip_poll(void)
{
    sys_check_timeouts();
    /* Ethernet RX pump is NOT here -- see header comment: that's your
     * board's ENET driver's job (ethernetif.c calling netif->input()). */
}
