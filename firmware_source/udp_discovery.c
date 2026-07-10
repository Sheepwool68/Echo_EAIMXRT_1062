#include "udp_discovery.h"
#include "pc_protocol.h"

int udp_service_discovery(const udp_transport_t *t, const uint8_t mac[6])
{
    uint8_t rx_buf[64];
    uint8_t reply[64];
    size_t reply_len = 0;
    uint32_t sender_ip;
    uint16_t sender_port;
    int n;
    int sent;

    n = t->recv_from(t->ctx, rx_buf, sizeof(rx_buf), &sender_ip, &sender_port);
    if (n <= 0) {
        return (n < 0) ? -1 : 0;
    }

    if (!pc_build_discovery_reply(mac, reply, sizeof(reply), &reply_len)) {
        return -1;
    }

    sent = t->send_to(t->ctx, reply, reply_len, sender_ip, sender_port);
    return (sent >= 0) ? 1 : -1;
}
