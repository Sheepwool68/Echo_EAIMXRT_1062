#include "fw_downloader.h"
#include "http_response_parser.h"
#include "crc32.h"
#include <string.h>
#include <stdio.h>

typedef struct {
    fw_download_sink_fn sink;
    void *sink_ctx;
    uint32_t crc;
    int sink_failed;
} body_relay_ctx_t;

static void body_chunk_relay(void *ctx, const uint8_t *data, size_t len)
{
    body_relay_ctx_t *b = (body_relay_ctx_t *)ctx;
    if (b->sink_failed) {
        return;
    }
    b->crc = crc32_update(b->crc, data, len);
    if (b->sink(b->sink_ctx, data, len) != 0) {
        b->sink_failed = 1;
    }
}

fw_download_result_t fw_download_firmware(const fw_http_transport_t *transport,
                                           const char *host, uint16_t port, const char *path,
                                           fw_download_sink_fn sink, void *sink_ctx,
                                           uint32_t connect_timeout_ms, uint32_t io_timeout_ms,
                                           fw_download_progress_t *out_progress)
{
    char request[256];
    int req_len;
    http_response_parser_t parser;
    uint8_t buf[512];
    body_relay_ctx_t body_ctx;

    if (out_progress != NULL) {
        out_progress->total_bytes = -1;
        out_progress->bytes_received = 0;
        out_progress->crc32 = 0;
    }

    if (transport->connect(transport->ctx, host, port, connect_timeout_ms) != 0) {
        return FW_DL_CONNECT_FAILED;
    }

    req_len = snprintf(request, sizeof(request),
                        "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                        path, host);
    if (req_len <= 0 || (size_t)req_len >= sizeof(request)
        || transport->send(transport->ctx, (const uint8_t *)request, (size_t)req_len) < 0) {
        transport->close(transport->ctx);
        return FW_DL_CONNECT_FAILED;
    }

    http_response_parser_init(&parser);

    body_ctx.sink = sink;
    body_ctx.sink_ctx = sink_ctx;
    body_ctx.crc = CRC32_INITIAL;
    body_ctx.sink_failed = 0;

    for (;;) {
        int n = transport->recv(transport->ctx, buf, sizeof(buf), io_timeout_ms);
        http_parse_state_t st;

        if (n < 0) {
            transport->close(transport->ctx);
            return FW_DL_TIMEOUT;
        }
        if (n == 0) {
            transport->close(transport->ctx);
            return FW_DL_TIMEOUT;
        }

        st = http_response_parser_feed(&parser, buf, (size_t)n, body_chunk_relay, &body_ctx);

        if (out_progress != NULL) {
            out_progress->total_bytes = parser.content_length;
            out_progress->bytes_received = parser.body_bytes_received;
        }

        if (st == HTTP_PARSE_ERROR) {
            transport->close(transport->ctx);
            return FW_DL_PARSE_ERROR;
        }
        if (body_ctx.sink_failed) {
            transport->close(transport->ctx);
            return FW_DL_SINK_ERROR;
        }
        if (st == HTTP_PARSE_DONE) {
            break;
        }
    }

    transport->close(transport->ctx);

    if (parser.status_code != 200) {
        return FW_DL_HTTP_ERROR;
    }

    if (out_progress != NULL) {
        out_progress->crc32 = crc32_finalize(body_ctx.crc);
    }

    return FW_DL_OK;
}
