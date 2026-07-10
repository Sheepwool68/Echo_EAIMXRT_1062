#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "tcp_session.h"

/* ------------------------------------------------------------------ */
/* Mock transport                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int alive;
    int established;
    int reopen_calls;
    int on_new_conn_calls;

    /* queued inbound data for recv() to hand back once */
    const uint8_t *inbound;
    size_t inbound_len;
    int inbound_consumed;

    /* record of everything sent, concatenated */
    uint8_t sent[512];
    size_t sent_len;
} mock_sock_t;

static int m_is_alive(void *ctx) { return ((mock_sock_t *)ctx)->alive; }
static int m_is_established(void *ctx) { return ((mock_sock_t *)ctx)->established; }
static void m_reopen(void *ctx) { ((mock_sock_t *)ctx)->reopen_calls++; }
static void m_on_new_conn(void *ctx) { ((mock_sock_t *)ctx)->on_new_conn_calls++; }

static int m_recv(void *ctx, uint8_t *buf, size_t max_len) {
    mock_sock_t *m = (mock_sock_t *)ctx;
    if (m->inbound_consumed || m->inbound == NULL) return 0;
    size_t n = (m->inbound_len < max_len) ? m->inbound_len : max_len;
    memcpy(buf, m->inbound, n);
    m->inbound_consumed = 1;
    return (int)n;
}
static int m_send(void *ctx, const uint8_t *buf, size_t len) {
    mock_sock_t *m = (mock_sock_t *)ctx;
    memcpy(m->sent + m->sent_len, buf, len);
    m->sent_len += len;
    return (int)len;
}

static tcp_socket_transport_t make_transport(mock_sock_t *m) {
    tcp_socket_transport_t t;
    t.ctx = m;
    t.is_alive = m_is_alive;
    t.is_established = m_is_established;
    t.recv = m_recv;
    t.send = m_send;
    t.reopen_listen = m_reopen;
    t.on_new_connection = m_on_new_conn;
    return t;
}

static void mock_reset(mock_sock_t *m) {
    memset(m, 0, sizeof(*m));
    m->alive = 1; /* default: socket healthy, just not established */
}

/* ------------------------------------------------------------------ */

static void test_not_alive_reopens_listener(void) {
    mock_sock_t m; mock_reset(&m);
    m.alive = 0;
    tcp_socket_transport_t t = make_transport(&m);
    tcp_socket_session_t s = {0};

    pc_parsed_command_t cmd;
    tcp_session_event_t ev = tcp_session_process(&t, &s, 0, 50, &cmd);

    assert(m.reopen_calls == 1);
    assert(ev == TCP_SESSION_IDLE); /* not established either */
    printf("test_not_alive_reopens_listener OK\n");
}

static void test_new_connection_sends_greeting_and_battery(void) {
    mock_sock_t m; mock_reset(&m);
    m.established = 1;
    tcp_socket_transport_t t = make_transport(&m);
    tcp_socket_session_t s = {0};

    pc_parsed_command_t cmd;
    tcp_session_event_t ev = tcp_session_process(&t, &s, 12345UL, 88, &cmd);

    assert(ev == TCP_SESSION_NEWLY_CONNECTED);
    assert(s.client_connected == 1);
    assert(m.on_new_conn_calls == 1);

    char expected[128];
    int n1 = snprintf(expected, sizeof(expected), "Connected,%lu,U\n", 12345UL);
    assert(memcmp(m.sent, expected, (size_t)n1) == 0);
    int n2 = snprintf(expected, sizeof(expected), "V=%d\n", 88);
    assert(memcmp(m.sent + n1, expected, (size_t)n2) == 0);
    assert(m.sent_len == (size_t)(n1 + n2));

    printf("test_new_connection_sends_greeting_and_battery OK\n");
}

static void test_disconnect_detected_once(void) {
    mock_sock_t m; mock_reset(&m);
    m.established = 1;
    tcp_socket_transport_t t = make_transport(&m);
    tcp_socket_session_t s = {0};
    pc_parsed_command_t cmd;

    /* first call: connects */
    tcp_session_process(&t, &s, 0, 50, &cmd);
    assert(s.client_connected == 1);

    /* client drops */
    m.established = 0;
    tcp_session_event_t ev = tcp_session_process(&t, &s, 0, 50, &cmd);
    assert(ev == TCP_SESSION_DISCONNECTED);
    assert(s.client_connected == 0);

    /* subsequent calls while still not established: IDLE, not repeated DISCONNECTED */
    ev = tcp_session_process(&t, &s, 0, 50, &cmd);
    assert(ev == TCP_SESSION_IDLE);

    printf("test_disconnect_detected_once OK\n");
}

static void test_command_received_and_classified(void) {
    mock_sock_t m; mock_reset(&m);
    m.established = 1;
    tcp_socket_transport_t t = make_transport(&m);
    tcp_socket_session_t s = {0};
    pc_parsed_command_t cmd;

    /* connect first */
    tcp_session_process(&t, &s, 0, 50, &cmd);
    m.sent_len = 0; /* reset send log */

    /* now simulate an incoming '?' (get reading status) command */
    static const uint8_t inbound[] = { '?' };
    m.inbound = inbound;
    m.inbound_len = sizeof(inbound);
    m.inbound_consumed = 0;

    tcp_session_event_t ev = tcp_session_process(&t, &s, 0, 50, &cmd);
    assert(ev == TCP_SESSION_COMMAND);
    assert(cmd.id == PC_CMD_GET_READING_STATUS);

    printf("test_command_received_and_classified OK\n");
}

static void test_broadcast_status_respects_interval(void) {
    mock_sock_t m0, m1; mock_reset(&m0); mock_reset(&m1);
    m0.established = 1; m1.established = 1;
    tcp_socket_transport_t transports[2] = { make_transport(&m0), make_transport(&m1) };
    tcp_socket_session_t sessions[2] = { {1}, {0} }; /* only slot 0 connected */

    uint32_t last_broadcast = 0;
    /* first call: not enough time elapsed since t=0 yet */
    int sent = tcp_broadcast_status(transports, sessions, 2, 42, 500 /*now*/, 2000, &last_broadcast);
    assert(sent == 0);
    assert(last_broadcast == 0);

    /* now past the interval: should send, only to the connected slot */
    sent = tcp_broadcast_status(transports, sessions, 2, 42, 2500, 2000, &last_broadcast);
    assert(sent == 1);
    assert(last_broadcast == 2500);

    char expected[32];
    int n = snprintf(expected, sizeof(expected), "V=%d\n", 42);
    assert(memcmp(m0.sent, expected, (size_t)n) == 0);
    assert(m1.sent_len == 0);

    /* too soon again: interval not elapsed, nothing sent */
    m0.sent_len = 0;
    sent = tcp_broadcast_status(transports, sessions, 2, 42, 3000, 2000, &last_broadcast);
    assert(sent == 0);
    assert(m0.sent_len == 0);

    /* interval elapsed again: sends once more */
    sent = tcp_broadcast_status(transports, sessions, 2, 42, 5600, 2000, &last_broadcast);
    assert(sent == 1);

    printf("test_broadcast_status_respects_interval OK\n");
}

static void test_reset_triggered(void) {
    mock_sock_t m; mock_reset(&m);
    tcp_socket_transport_t t = make_transport(&m);

    assert(tcp_reset_triggered(&t) == 0);
    m.established = 1;
    assert(tcp_reset_triggered(&t) == 1);

    printf("test_reset_triggered OK\n");
}

int main(void) {
    test_not_alive_reopens_listener();
    test_new_connection_sends_greeting_and_battery();
    test_disconnect_detected_once();
    test_command_received_and_classified();
    test_broadcast_status_respects_interval();
    test_reset_triggered();
    printf("\nAll tcp_session tests passed.\n");
    return 0;
}
