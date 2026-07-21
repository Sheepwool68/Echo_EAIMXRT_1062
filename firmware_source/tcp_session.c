/*
 * tcp_session.c
 *
 * See tcp_session.h for porting rationale.
 */

#include "tcp_session.h"
#include "debug_console_rt1062.h"

/* PRINTF redirect to LPUART5 -- added 2026-07-21 for the connect-greeting
 * send tracing below (TEMPORARY DIAGNOSTIC), see that call site's own
 * comment. host_tests/Makefile's test_tcp_session_EXTRA pulls in the
 * debug_console_stub.c stub so this still links on the host. */
#undef PRINTF
#define PRINTF debug_printf

#define GREETING_BUF_SIZE 64
#define STATUS_BUF_SIZE   32
#define RECV_BUF_SIZE     64

tcp_session_event_t tcp_session_process(const tcp_socket_transport_t *t,
                                         tcp_socket_session_t *session,
                                         unsigned long last_time_sent,
                                         int batt_percent,
                                         pc_parsed_command_t *out_cmd)
{
    if (!t->is_alive(t->ctx)) {
        t->reopen_listen(t->ctx);
    }

    if (!t->is_established(t->ctx)) {
        if (session->client_connected) {
            session->client_connected = 0;
            return TCP_SESSION_DISCONNECTED;
        }
        return TCP_SESSION_IDLE;
    }

    if (!session->client_connected) {
        char greeting[GREETING_BUF_SIZE];
        char status[STATUS_BUF_SIZE];
        int n;

        /* TEMPORARY DIAGNOSTIC, added 2026-07-21 per explicit report
         * ("You are not sending the connected string on first socket
         * connection") -- send()'s return value was never checked here,
         * same gap already found and traced in process_nrf_spi()'s
         * chip-read send. LSOC lighting confirms this branch genuinely
         * runs (it's the same TCP_SESSION_NEWLY_CONNECTED event that
         * drives the LED), so the open question is specifically whether
         * these two sends succeed at the transport layer. Remove once
         * this report is resolved. */
        n = pc_build_connect_greeting(last_time_sent, greeting, sizeof(greeting));
        if (n > 0) {
            int sent;
            /* TEMPORARY DIAGNOSTIC, added 2026-07-21 -- confirms the
             * SOURCE buffer is correct (last byte really is 0x0a) right
             * before it's handed to the transport, to rule in/out
             * pc_build_connect_greeting() itself vs. something further
             * down the send path silently dropping the trailing byte.
             * Remove once this report is resolved. */
            PRINTF("greeting source: n=%d last_byte=0x%02x\r\n", n, (unsigned char)greeting[n - 1]);
            sent = t->send(t->ctx, (const uint8_t *)greeting, (size_t)n);
            PRINTF("TCP connect greeting send: requested=%d actual=%d\r\n", n, sent);
        }
        n = pc_format_battery_status(batt_percent, status, sizeof(status));
        if (n > 0) {
            int sent = t->send(t->ctx, (const uint8_t *)status, (size_t)n);
            PRINTF("TCP connect battery-status send: requested=%d actual=%d\r\n", n, sent);
        }

        session->client_connected = 1;
        t->on_new_connection(t->ctx);
        return TCP_SESSION_NEWLY_CONNECTED;
    }

    /* Already connected: check for an incoming command */
    {
        uint8_t recv_buf[RECV_BUF_SIZE];
        int n = t->recv(t->ctx, recv_buf, sizeof(recv_buf));
        if (n > 0) {
            *out_cmd = pc_classify_command(recv_buf, (size_t)n);
            return TCP_SESSION_COMMAND;
        }
    }

    return TCP_SESSION_IDLE;
}

int tcp_broadcast_status(const tcp_socket_transport_t *transports,
                          const tcp_socket_session_t *sessions,
                          size_t count,
                          int batt_percent,
                          uint32_t now_ms,
                          uint32_t interval_ms,
                          uint32_t *last_broadcast_ms)
{
    char status[STATUS_BUF_SIZE];
    int n;
    size_t i;
    int sent_count = 0;

    if (now_ms - *last_broadcast_ms <= interval_ms) {
        return 0;
    }

    n = pc_format_battery_status(batt_percent, status, sizeof(status));
    if (n <= 0) {
        return 0;
    }

    for (i = 0; i < count; i++) {
        if (sessions[i].client_connected) {
            transports[i].send(transports[i].ctx, (const uint8_t *)status, (size_t)n);
            sent_count++;
        }
    }

    *last_broadcast_ms = now_ms;
    return sent_count;
}

int tcp_reset_triggered(const tcp_socket_transport_t *reset_transport)
{
    return reset_transport->is_established(reset_transport->ctx) ? 1 : 0;
}
