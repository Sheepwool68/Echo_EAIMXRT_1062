#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "udp_discovery.h"

typedef struct {
    const uint8_t *inbound;
    size_t inbound_len;
    int consumed;
    uint32_t expect_ip;
    uint16_t expect_port;

    uint8_t sent[64];
    size_t sent_len;
    uint32_t sent_ip;
    uint16_t sent_port;
    int force_recv_error;
} mock_udp_t;

static int m_recv_from(void *ctx, uint8_t *buf, size_t max_len, uint32_t *ip, uint16_t *port) {
    mock_udp_t *m = (mock_udp_t *)ctx;
    if (m->force_recv_error) return -1;
    if (m->consumed || m->inbound == NULL) return 0;
    size_t n = (m->inbound_len < max_len) ? m->inbound_len : max_len;
    memcpy(buf, m->inbound, n);
    *ip = m->expect_ip;
    *port = m->expect_port;
    m->consumed = 1;
    return (int)n;
}
static int m_send_to(void *ctx, const uint8_t *buf, size_t len, uint32_t ip, uint16_t port) {
    mock_udp_t *m = (mock_udp_t *)ctx;
    memcpy(m->sent, buf, len);
    m->sent_len = len;
    m->sent_ip = ip;
    m->sent_port = port;
    return (int)len;
}

static void test_no_pending_datagram(void) {
    mock_udp_t m; memset(&m, 0, sizeof(m));
    udp_transport_t t = { &m, m_recv_from, m_send_to };
    uint8_t mac[6] = {1,2,3,4,5,6};

    int r = udp_service_discovery(&t, mac);
    assert(r == 0);
    assert(m.sent_len == 0);
    printf("test_no_pending_datagram OK\n");
}

static void test_discovery_request_answered(void) {
    mock_udp_t m; memset(&m, 0, sizeof(m));
    static const uint8_t probe[] = { 0x01 }; /* content doesn't matter to this responder */
    m.inbound = probe;
    m.inbound_len = sizeof(probe);
    m.expect_ip = 0xC0A80105; /* 192.168.1.5 */
    m.expect_port = 5000;

    udp_transport_t t = { &m, m_recv_from, m_send_to };
    uint8_t mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 };

    int r = udp_service_discovery(&t, mac);
    assert(r == 1);
    assert(m.sent_len == 38);
    assert(m.sent_ip == 0xC0A80105);
    assert(m.sent_port == 5000);
    assert(memcmp(&m.sent[21], "de:ad:be:ef:00:01", 17) == 0);

    printf("test_discovery_request_answered OK\n");
}

static void test_recv_error_propagates(void) {
    mock_udp_t m; memset(&m, 0, sizeof(m));
    m.force_recv_error = 1;
    udp_transport_t t = { &m, m_recv_from, m_send_to };
    uint8_t mac[6] = {0};

    int r = udp_service_discovery(&t, mac);
    assert(r < 0);
    printf("test_recv_error_propagates OK\n");
}

int main(void) {
    test_no_pending_datagram();
    test_discovery_request_answered();
    test_recv_error_propagates();
    printf("\nAll udp_discovery tests passed.\n");
    return 0;
}
