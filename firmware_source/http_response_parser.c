#include "http_response_parser.h"
#include <string.h>
#include <stdlib.h>

/* Case-insensitive prefix compare, written locally rather than relying
 * on strncasecmp (POSIX extension, not guaranteed available under a
 * strict C11 build depending on your libc configuration). */
static int starts_with_ci(const char *s, const char *prefix)
{
    while (*prefix) {
        char a = *s++;
        char b = *prefix++;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

void http_response_parser_init(http_response_parser_t *p)
{
    memset(p, 0, sizeof(*p));
    p->state = HTTP_PARSE_STATUS_LINE;
    p->content_length = -1;
    p->body_bytes_received = 0;
}

http_parse_state_t http_response_parser_feed(http_response_parser_t *p,
                                              const uint8_t *data, size_t len,
                                              http_body_chunk_cb body_cb, void *body_cb_ctx)
{
    size_t i = 0;

    while (i < len && p->state != HTTP_PARSE_DONE && p->state != HTTP_PARSE_ERROR) {

        if (p->state == HTTP_PARSE_BODY) {
            size_t remaining_in_buf = len - i;
            size_t chunk_len = remaining_in_buf;

            if (p->content_length >= 0) {
                long remaining_body = p->content_length - p->body_bytes_received;
                if (remaining_body < 0) {
                    remaining_body = 0;
                }
                if ((long)chunk_len > remaining_body) {
                    chunk_len = (size_t)remaining_body;
                }
            }

            if (chunk_len > 0) {
                if (body_cb != NULL) {
                    body_cb(body_cb_ctx, &data[i], chunk_len);
                }
                p->body_bytes_received += (long)chunk_len;
                i += chunk_len;
            }

            if (p->content_length >= 0 && p->body_bytes_received >= p->content_length) {
                p->state = HTTP_PARSE_DONE;
            } else if (chunk_len == 0) {
                break;
            }
            continue;
        }

        {
            uint8_t c = data[i++];

            if (c == '\r') {
                continue;
            }

            if (c == '\n') {
                p->line_buf[p->line_len] = '\0';

                if (p->state == HTTP_PARSE_STATUS_LINE) {
                    char *sp = strchr(p->line_buf, ' ');
                    if (sp == NULL) {
                        p->state = HTTP_PARSE_ERROR;
                        return p->state;
                    }
                    p->status_code = atoi(sp + 1);
                    p->state = HTTP_PARSE_HEADERS;
                } else {
                    if (p->line_len == 0) {
                        p->state = (p->content_length == 0) ? HTTP_PARSE_DONE : HTTP_PARSE_BODY;
                    } else if (starts_with_ci(p->line_buf, "Content-Length:")) {
                        const char *v = p->line_buf + 15;
                        while (*v == ' ') {
                            v++;
                        }
                        p->content_length = atol(v);
                    }
                }
                p->line_len = 0;
            } else {
                if (p->line_len < sizeof(p->line_buf) - 1) {
                    p->line_buf[p->line_len++] = (char)c;
                }
            }
        }
    }

    return p->state;
}
