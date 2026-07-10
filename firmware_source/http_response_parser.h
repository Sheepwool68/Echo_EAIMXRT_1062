/*
 * http_response_parser.h
 *
 * New infrastructure for the OTA firmware download path -- not part of
 * the original firmware, which relied on Dynamic C's http_client.lib
 * for this (a library whose internals weren't available to port).
 *
 * A minimal, INCREMENTAL HTTP/1.1 response parser: feed it bytes as
 * they arrive from the network (matching how lwIP's raw API delivers
 * data -- a callback per received TCP segment, not one big buffer),
 * and it tracks progress through status-line -> headers -> body,
 * extracting Content-Length and handing body bytes to a callback as
 * they're recognized.
 *
 * Deliberately narrow scope: assumes Content-Length framing (not
 * chunked transfer-encoding), and assumes a 200 OK success path is
 * what you care about (other status codes are reported, not
 * specially handled). This matches a simple static file server
 * serving a firmware binary -- if your actual server does chunked
 * encoding, this needs extending before it'll work against it.
 */

#ifndef HTTP_RESPONSE_PARSER_H
#define HTTP_RESPONSE_PARSER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_PARSE_STATUS_LINE,
    HTTP_PARSE_HEADERS,
    HTTP_PARSE_BODY,
    HTTP_PARSE_DONE,
    HTTP_PARSE_ERROR,
} http_parse_state_t;

typedef struct {
    http_parse_state_t state;
    int status_code;
    long content_length;
    long body_bytes_received;

    char line_buf[256];
    size_t line_len;
} http_response_parser_t;

typedef void (*http_body_chunk_cb)(void *ctx, const uint8_t *data, size_t len);

void http_response_parser_init(http_response_parser_t *p);

/*
 * Feeds len bytes of raw socket data through the parser. Calls
 * body_cb once per recognized chunk of body data (may be called
 * multiple times per feed call, or zero times if still in headers).
 * Returns the parser's state after processing this chunk -- check for
 * HTTP_PARSE_ERROR or HTTP_PARSE_DONE after each call.
 */
http_parse_state_t http_response_parser_feed(http_response_parser_t *p,
                                              const uint8_t *data, size_t len,
                                              http_body_chunk_cb body_cb, void *body_cb_ctx);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_RESPONSE_PARSER_H */
