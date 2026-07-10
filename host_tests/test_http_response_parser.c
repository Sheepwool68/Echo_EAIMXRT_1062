#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "http_response_parser.h"

static uint8_t g_body[256];
static size_t g_body_len;

static void collect_body(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    memcpy(&g_body[g_body_len], data, len);
    g_body_len += len;
}

static void reset_collector(void) { g_body_len = 0; }

static void test_full_response_one_shot(void) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/octet-stream\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "ABCDE";

    http_response_parser_t p;
    http_response_parser_init(&p);
    reset_collector();

    http_parse_state_t st = http_response_parser_feed(&p, (const uint8_t *)resp, strlen(resp),
                                                        collect_body, NULL);

    assert(st == HTTP_PARSE_DONE);
    assert(p.status_code == 200);
    assert(p.content_length == 5);
    assert(g_body_len == 5);
    assert(memcmp(g_body, "ABCDE", 5) == 0);
    printf("test_full_response_one_shot OK\n");
}

static void test_fed_byte_by_byte(void) {
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Length: 3\r\n"
        "\r\n"
        "XYZ";

    http_response_parser_t p;
    http_response_parser_init(&p);
    reset_collector();

    size_t i;
    http_parse_state_t st = HTTP_PARSE_STATUS_LINE;
    for (i = 0; i < strlen(resp); i++) {
        st = http_response_parser_feed(&p, (const uint8_t *)&resp[i], 1, collect_body, NULL);
    }

    assert(st == HTTP_PARSE_DONE);
    assert(g_body_len == 3);
    assert(memcmp(g_body, "XYZ", 3) == 0);
    printf("test_fed_byte_by_byte OK\n");
}

static void test_split_across_multiple_feeds(void) {
    const char *seg1 = "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\n";
    const char *seg2 = "ABC";
    const char *seg3 = "DEF";

    http_response_parser_t p;
    http_response_parser_init(&p);
    reset_collector();

    http_response_parser_feed(&p, (const uint8_t *)seg1, strlen(seg1), collect_body, NULL);
    assert(p.state == HTTP_PARSE_BODY);
    http_response_parser_feed(&p, (const uint8_t *)seg2, strlen(seg2), collect_body, NULL);
    http_parse_state_t st = http_response_parser_feed(&p, (const uint8_t *)seg3, strlen(seg3), collect_body, NULL);

    assert(st == HTTP_PARSE_DONE);
    assert(g_body_len == 6);
    assert(memcmp(g_body, "ABCDEF", 6) == 0);
    printf("test_split_across_multiple_feeds OK\n");
}

static void test_case_insensitive_header_name(void) {
    const char *resp = "HTTP/1.1 200 OK\r\ncontent-length: 2\r\n\r\nHI";
    http_response_parser_t p;
    http_response_parser_init(&p);
    reset_collector();

    http_parse_state_t st = http_response_parser_feed(&p, (const uint8_t *)resp, strlen(resp),
                                                        collect_body, NULL);
    assert(st == HTTP_PARSE_DONE);
    assert(p.content_length == 2);
    printf("test_case_insensitive_header_name OK\n");
}

static void test_non_200_status_reported_not_specially_handled(void) {
    const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    http_response_parser_t p;
    http_response_parser_init(&p);
    reset_collector();

    http_parse_state_t st = http_response_parser_feed(&p, (const uint8_t *)resp, strlen(resp),
                                                        collect_body, NULL);
    assert(p.status_code == 404);
    assert(st == HTTP_PARSE_DONE);
    assert(g_body_len == 0);
    printf("test_non_200_status_reported_not_specially_handled OK\n");
}

static void test_extra_bytes_beyond_content_length_ignored(void) {
    const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nABCEXTRA";
    http_response_parser_t p;
    http_response_parser_init(&p);
    reset_collector();

    http_parse_state_t st = http_response_parser_feed(&p, (const uint8_t *)resp, strlen(resp),
                                                        collect_body, NULL);
    assert(st == HTTP_PARSE_DONE);
    assert(g_body_len == 3);
    assert(memcmp(g_body, "ABC", 3) == 0);
    printf("test_extra_bytes_beyond_content_length_ignored OK\n");
}

int main(void) {
    test_full_response_one_shot();
    test_fed_byte_by_byte();
    test_split_across_multiple_feeds();
    test_case_insensitive_header_name();
    test_non_200_status_reported_not_specially_handled();
    test_extra_bytes_beyond_content_length_ignored();
    printf("\nAll http_response_parser tests passed.\n");
    return 0;
}
