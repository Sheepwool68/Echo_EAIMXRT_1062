/*
 * udp_discovery.h
 *
 * Portable re-implementation of the UDP half of ProcessResetSocket()
 * -- answering LAN discovery broadcasts on port 2000 (was
 * udp_extopen(&UDPSocket...)) with the device's identity packet built
 * by pc_build_discovery_reply().
 */

#ifndef UDP_DISCOVERY_H
#define UDP_DISCOVERY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;
    /*
     * Non-blocking receive. Returns:
     *   > 0 : that many bytes were received; out_ip/out_port are set to
     *         the sender's address (for the reply)
     *   0   : nothing pending
     *   < 0 : error
     */
    int (*recv_from)(void *ctx, uint8_t *buf, size_t max_len,
                      uint32_t *out_ip, uint16_t *out_port);
    /* Returns bytes sent, or < 0 on error. */
    int (*send_to)(void *ctx, const uint8_t *buf, size_t len,
                    uint32_t ip, uint16_t port);
} udp_transport_t;

/*
 * Services one pending discovery datagram, if any.
 * Returns 1 if a request was received and answered, 0 if nothing was
 * pending, negative on I/O error.
 */
int udp_service_discovery(const udp_transport_t *t, const uint8_t mac[6]);

#ifdef __cplusplus
}
#endif

#endif /* UDP_DISCOVERY_H */
