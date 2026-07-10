#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "uhf_reader.h"

#define MAX_WRITES 32
typedef struct {
    uint8_t data[64];
    size_t len;
} recorded_write_t;

typedef struct {
    int open_calls;
    uint32_t last_baud;
    int flush_rx_calls;
    int flush_tx_calls;

    recorded_write_t writes[MAX_WRITES];
    int write_count;

    const uint8_t *canned_reply;
    size_t canned_reply_len;
} mock_uart_t;

static int m_open(void *ctx, uint32_t baud) {
    mock_uart_t *m = (mock_uart_t *)ctx;
    m->open_calls++;
    m->last_baud = baud;
    return 0;
}
static int m_write(void *ctx, const uint8_t *buf, size_t len) {
    mock_uart_t *m = (mock_uart_t *)ctx;
    if (m->write_count < MAX_WRITES) {
        memcpy(m->writes[m->write_count].data, buf, len);
        m->writes[m->write_count].len = len;
        m->write_count++;
    }
    return (int)len;
}
static int m_read(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms) {
    mock_uart_t *m = (mock_uart_t *)ctx;
    (void)timeout_ms;
    if (m->canned_reply == NULL) return 0;
    size_t n = (m->canned_reply_len < max_len) ? m->canned_reply_len : max_len;
    memcpy(buf, m->canned_reply, n);
    return (int)n;
}
static void m_flush_rx(void *ctx) { ((mock_uart_t *)ctx)->flush_rx_calls++; }
static void m_flush_tx(void *ctx) { ((mock_uart_t *)ctx)->flush_tx_calls++; }
static void m_delay(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }
static void m_close(void *ctx) { (void)ctx; }

static void mock_reset(mock_uart_t *m) {
    memset(m, 0, sizeof(*m));
}

static uhf_transport_t make_transport(mock_uart_t *m) {
    uhf_transport_t t;
    t.ctx = m;
    t.open = m_open;
    t.close = m_close;
    t.write = m_write;
    t.read = m_read;
    t.flush_rx = m_flush_rx;
    t.flush_tx = m_flush_tx;
    t.delay_ms = m_delay;
    return t;
}

static void test_open_sends_version_query(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;

    int rc = uhf_reader_open(&r, &t);

    assert(rc == 0);
    assert(m.open_calls == 1);
    assert(m.last_baud == 115200);
    assert(m.flush_rx_calls == 1 && m.flush_tx_calls == 1);
    assert(m.write_count == 1);
    assert(m.writes[0].len == 5);
    assert(m.writes[0].data[0] == 0xff && m.writes[0].data[2] == 0x03);

    printf("test_open_sends_version_query OK\n");
}

static void test_start_aborts_with_no_antennas(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;
    r.ants = 0;

    int rc = uhf_reader_start(&r, 0);
    assert(rc == 0);
    assert(m.write_count == 0);

    printf("test_start_aborts_with_no_antennas OK\n");
}

static void test_start_sends_command_when_antennas_present(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;
    r.ants = 0x0F;

    int rc = uhf_reader_start(&r, 0);
    assert(rc == 1);
    assert(m.write_count == 1);
    assert(m.writes[0].len == 24);
    assert(m.writes[0].data[0] == 0xFF && m.writes[0].data[1] == 0x13);

    printf("test_start_sends_command_when_antennas_present OK\n");
}

static void test_stop_sends_stop_then_temperature_query(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    int rc = uhf_reader_stop(&r);
    assert(rc == 0);
    assert(m.write_count == 2);
    assert(m.writes[0].len == 19);
    assert(m.writes[0].data[1] == 0x0E);
    assert(m.writes[1].len == 5);
    assert(m.writes[1].data[2] == 0x72);

    printf("test_stop_sends_stop_then_temperature_query OK\n");
}

static void test_get_temperature_parses_reply(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    static const uint8_t reply[8] = {0xFF,0x00,0x72,0x00,0x00,37,0x00,0x00};
    m.canned_reply = reply;
    m.canned_reply_len = sizeof(reply);

    int temp = -1;
    int rc = uhf_reader_get_temperature(&r, &temp);
    assert(rc == 1);
    assert(temp == 37);

    printf("test_get_temperature_parses_reply OK\n");
}

static void test_set_antennae_updates_ants_from_dc_check_reply(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    static const uint8_t reply[14] = {0xFF,0x09,0x61,0x00,0x00,0x05,0x01,0x01,0x02,0x01,0x03,0x01,0x04,0x01};
    m.canned_reply = reply;
    m.canned_reply_len = sizeof(reply);

    int rc = uhf_reader_set_antennae(&r, UHF_REGION_FCC);
    assert(rc == 0);
    assert(r.ants == 0x0F);

    assert(m.write_count == 3);
    assert(m.writes[0].data[2] == 0x61);
    assert(m.writes[1].data[1] == 0x09 && m.writes[1].data[2] == 0x91);
    assert(m.writes[2].data[1] == 0x15 && m.writes[2].data[2] == 0x91);

    printf("test_set_antennae_updates_ants_from_dc_check_reply OK\n");
}

static void test_set_antennae_runs_rl_test_when_not_all_connected(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    static const uint8_t dc_reply[14] = {0xFF,0x09,0x61,0x00,0x00,0x05,0x01,0x00,0x02,0x00,0x03,0x00,0x04,0x00};
    m.canned_reply = dc_reply;
    m.canned_reply_len = sizeof(dc_reply);

    uhf_reader_set_antennae(&r, UHF_REGION_FCC);

    assert(m.write_count == 6);
    for (int i = 1; i <= 4; i++) {
        assert(m.writes[i].data[1] == 0x16);
    }

    printf("test_set_antennae_runs_rl_test_when_not_all_connected OK\n");
}

int main(void) {
    test_open_sends_version_query();
    test_start_aborts_with_no_antennas();
    test_start_sends_command_when_antennas_present();
    test_stop_sends_stop_then_temperature_query();
    test_get_temperature_parses_reply();
    test_set_antennae_updates_ants_from_dc_check_reply();
    test_set_antennae_runs_rl_test_when_not_all_connected();
    printf("\nAll uhf_reader orchestration tests passed.\n");
    return 0;
}
