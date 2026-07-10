#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "fw_downloader.h"
#include "crc32.h"

typedef struct {
    int connect_calls, close_calls;
    int connect_should_fail;

    const uint8_t *response;
    size_t response_len;
    size_t response_pos;
    size_t chunk_size;

    int force_recv_error_after;
    int recv_call_count;

    char last_request[512];
    size_t last_request_len;
} mock_http_t;

static int m_connect(void *ctx, const char *host, uint16_t port, uint32_t timeout_ms) {
    mock_http_t *m = (mock_http_t *)ctx;
    (void)host; (void)port; (void)timeout_ms;
    m->connect_calls++;
    return m->connect_should_fail ? -1 : 0;
}
static int m_send(void *ctx, const uint8_t *buf, size_t len) {
    mock_http_t *m = (mock_http_t *)ctx;
    memcpy(m->last_request, buf, len);
    m->last_request_len = len;
    return (int)len;
}
static int m_recv(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms) {
    mock_http_t *m = (mock_http_t *)ctx;
    (void)timeout_ms;
    m->recv_call_count++;

    if (m->force_recv_error_after >= 0 && m->recv_call_count > m->force_recv_error_after) {
        return -1;
    }
    if (m->response_pos >= m->response_len) {
        return 0;
    }
    {
        size_t remaining = m->response_len - m->response_pos;
        size_t n = (m->chunk_size < remaining) ? m->chunk_size : remaining;
        if (n > max_len) n = max_len;
        memcpy(buf, &m->response[m->response_pos], n);
        m->response_pos += n;
        return (int)n;
    }
}
static void m_close(void *ctx) { ((mock_http_t *)ctx)->close_calls++; }

static void mock_reset(mock_http_t *m) {
    memset(m, 0, sizeof(*m));
    m->chunk_size = 4096;
    m->force_recv_error_after = -1;
}

static fw_http_transport_t make_transport(mock_http_t *m) {
    fw_http_transport_t t;
    t.ctx = m;
    t.connect = m_connect;
    t.send = m_send;
    t.recv = m_recv;
    t.close = m_close;
    return t;
}

static uint8_t g_sink_buf[4096];
static size_t g_sink_len;
static int g_sink_fail_after;
static int g_sink_call_count;

static int test_sink(void *ctx, const uint8_t *data, size_t len) {
    (void)ctx;
    g_sink_call_count++;
    if (g_sink_fail_after >= 0 && g_sink_call_count > g_sink_fail_after) {
        return -1;
    }
    memcpy(&g_sink_buf[g_sink_len], data, len);
    g_sink_len += len;
    return 0;
}
static void sink_reset(void) { g_sink_len = 0; g_sink_fail_after = -1; g_sink_call_count = 0; }

static void test_successful_download(void) {
    static const char body[] = "FIRMWARE_BYTES_HERE_1234567890";
    char resp[256];
    int resp_len = snprintf(resp, sizeof(resp),
                             "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",
                             strlen(body), body);

    mock_http_t m; mock_reset(&m);
    m.response = (const uint8_t *)resp;
    m.response_len = (size_t)resp_len;

    fw_http_transport_t t = make_transport(&m);
    sink_reset();

    fw_download_progress_t progress;
    fw_download_result_t r = fw_download_firmware(&t, "example.com", 80, "/fw.bin",
                                                    test_sink, NULL, 1000, 1000, &progress);

    assert(r == FW_DL_OK);
    assert(g_sink_len == strlen(body));
    assert(memcmp(g_sink_buf, body, strlen(body)) == 0);
    assert(progress.bytes_received == (long)strlen(body));
    assert(progress.crc32 == crc32_compute((const uint8_t *)body, strlen(body)));
    assert(m.connect_calls == 1 && m.close_calls == 1);
    assert(strstr(m.last_request, "GET /fw.bin HTTP/1.1") != NULL);
    assert(strstr(m.last_request, "Host: example.com") != NULL);

    printf("test_successful_download OK\n");
}

static void test_multi_chunk_delivery(void) {
    static const char body[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char resp[256];
    int resp_len = snprintf(resp, sizeof(resp),
                             "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",
                             strlen(body), body);

    mock_http_t m; mock_reset(&m);
    m.response = (const uint8_t *)resp;
    m.response_len = (size_t)resp_len;
    m.chunk_size = 5;

    fw_http_transport_t t = make_transport(&m);
    sink_reset();

    fw_download_progress_t progress;
    fw_download_result_t r = fw_download_firmware(&t, "example.com", 80, "/fw.bin",
                                                    test_sink, NULL, 1000, 1000, &progress);

    assert(r == FW_DL_OK);
    assert(g_sink_len == strlen(body));
    assert(memcmp(g_sink_buf, body, strlen(body)) == 0);
    assert(progress.crc32 == crc32_compute((const uint8_t *)body, strlen(body)));

    printf("test_multi_chunk_delivery OK\n");
}

static void test_connect_failure(void) {
    mock_http_t m; mock_reset(&m);
    m.connect_should_fail = 1;
    fw_http_transport_t t = make_transport(&m);
    sink_reset();

    fw_download_result_t r = fw_download_firmware(&t, "example.com", 80, "/fw.bin",
                                                    test_sink, NULL, 1000, 1000, NULL);
    assert(r == FW_DL_CONNECT_FAILED);
    assert(m.close_calls == 0);
    printf("test_connect_failure OK\n");
}

static void test_http_error_status(void) {
    const char *resp = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    mock_http_t m; mock_reset(&m);
    m.response = (const uint8_t *)resp;
    m.response_len = strlen(resp);
    fw_http_transport_t t = make_transport(&m);
    sink_reset();

    fw_download_result_t r = fw_download_firmware(&t, "example.com", 80, "/missing.bin",
                                                    test_sink, NULL, 1000, 1000, NULL);
    assert(r == FW_DL_HTTP_ERROR);
    assert(m.close_calls == 1);
    printf("test_http_error_status OK\n");
}

static void test_sink_rejection_aborts_download(void) {
    static const char body[] = "AAAAABBBBBCCCCCDDDDD";
    char resp[256];
    int resp_len = snprintf(resp, sizeof(resp),
                             "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s",
                             strlen(body), body);

    mock_http_t m; mock_reset(&m);
    m.response = (const uint8_t *)resp;
    m.response_len = (size_t)resp_len;
    m.chunk_size = 5;

    fw_http_transport_t t = make_transport(&m);
    sink_reset();
    g_sink_fail_after = 1;

    fw_download_result_t r = fw_download_firmware(&t, "example.com", 80, "/fw.bin",
                                                    test_sink, NULL, 1000, 1000, NULL);
    assert(r == FW_DL_SINK_ERROR);
    assert(m.close_calls == 1);
    printf("test_sink_rejection_aborts_download OK\n");
}

static void test_recv_timeout_mid_stream(void) {
    static const char body[] = "PARTIAL_ONLY";
    char resp[256];
    int resp_len = snprintf(resp, sizeof(resp),
                             "HTTP/1.1 200 OK\r\nContent-Length: 999\r\n\r\n%s",
                             body);

    mock_http_t m; mock_reset(&m);
    m.response = (const uint8_t *)resp;
    m.response_len = (size_t)resp_len;

    fw_http_transport_t t = make_transport(&m);
    sink_reset();

    fw_download_result_t r = fw_download_firmware(&t, "example.com", 80, "/fw.bin",
                                                    test_sink, NULL, 1000, 1000, NULL);
    assert(r == FW_DL_TIMEOUT);
    printf("test_recv_timeout_mid_stream OK\n");
}

int main(void) {
    test_successful_download();
    test_multi_chunk_delivery();
    test_connect_failure();
    test_http_error_status();
    test_sink_rejection_aborts_download();
    test_recv_timeout_mid_stream();
    printf("\nAll fw_downloader tests passed.\n");
    return 0;
}
