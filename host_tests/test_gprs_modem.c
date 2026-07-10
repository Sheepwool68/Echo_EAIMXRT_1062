#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "gprs_modem.h"

#define MAX_WRITES 32
typedef struct {
    uint8_t data[64];
    size_t len;
} recorded_write_t;

typedef struct {
    int open_calls;
    int close_calls;
    uint32_t last_baud;
    int flush_rx_calls, flush_tx_calls;
    int wake_pin_level;
    int power_enable_level;
    int wake_pin_set_count;
    int power_enable_set_count;

    recorded_write_t writes[MAX_WRITES];
    int write_count;

    const uint8_t *canned_reply;
    size_t canned_reply_len;
} mock_modem_t;

static int m_open(void *ctx, uint32_t baud) {
    mock_modem_t *m = (mock_modem_t *)ctx;
    m->open_calls++; m->last_baud = baud;
    return 0;
}
static void m_close(void *ctx) { ((mock_modem_t *)ctx)->close_calls++; }
static int m_write(void *ctx, const uint8_t *buf, size_t len) {
    mock_modem_t *m = (mock_modem_t *)ctx;
    if (m->write_count < MAX_WRITES) {
        memcpy(m->writes[m->write_count].data, buf, len);
        m->writes[m->write_count].len = len;
        m->write_count++;
    }
    return (int)len;
}
static int m_read(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms) {
    mock_modem_t *m = (mock_modem_t *)ctx;
    (void)timeout_ms;
    if (m->canned_reply == NULL) return 0;
    size_t n = (m->canned_reply_len < max_len) ? m->canned_reply_len : max_len;
    memcpy(buf, m->canned_reply, n);
    return (int)n;
}
static void m_flush_rx(void *ctx) { ((mock_modem_t *)ctx)->flush_rx_calls++; }
static void m_flush_tx(void *ctx) { ((mock_modem_t *)ctx)->flush_tx_calls++; }
static void m_delay(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }
static void m_set_wake(void *ctx, int level) {
    mock_modem_t *m = (mock_modem_t *)ctx;
    m->wake_pin_level = level; m->wake_pin_set_count++;
}
static void m_set_power(void *ctx, int level) {
    mock_modem_t *m = (mock_modem_t *)ctx;
    m->power_enable_level = level; m->power_enable_set_count++;
}

static void mock_reset(mock_modem_t *m) { memset(m, 0, sizeof(*m)); }

static gprs_transport_t make_transport(mock_modem_t *m) {
    gprs_transport_t t;
    t.ctx = m;
    t.open = m_open;
    t.close = m_close;
    t.write = m_write;
    t.read = m_read;
    t.flush_rx = m_flush_rx;
    t.flush_tx = m_flush_tx;
    t.delay_ms = m_delay;
    t.set_wake_pin = m_set_wake;
    t.set_power_enable = m_set_power;
    return t;
}

static int writes_contain(mock_modem_t *m, const char *needle) {
    int i;
    for (i = 0; i < m->write_count; i++) {
        if (m->writes[i].len == strlen(needle)
            && memcmp(m->writes[i].data, needle, m->writes[i].len) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_toggle_wake_sequence(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);

    gprs_modem_toggle(&m, 1);

    assert(mock.wake_pin_set_count == 1 && mock.wake_pin_level == 0);
    assert(mock.power_enable_set_count == 1 && mock.power_enable_level == 1);
    assert(mock.open_calls == 1 && mock.last_baud == 115200);
    assert(mock.flush_rx_calls == 1 && mock.flush_tx_calls == 1);

    assert(writes_contain(&mock, "AT\r"));
    assert(writes_contain(&mock, "ATE0\r"));
    assert(writes_contain(&mock, "AT&D1\r"));
    assert(writes_contain(&mock, "AT+QCFG=\"ledmode\",0\r"));
    assert(mock.write_count == 4);

    printf("test_toggle_wake_sequence OK\n");
}

static void test_toggle_sleep_sequence_not_connected(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);
    m.gprs_state = GPRS_STATE_NOGPRS;

    gprs_modem_toggle(&m, 0);

    assert(!writes_contain(&mock, "AT+QICLOSE=0\r"));
    assert(writes_contain(&mock, "AT+QCFG=\"ledmode\",2\r"));
    assert(writes_contain(&mock, "AT+QSCLK=1\r"));
    assert(mock.wake_pin_level == 1);
    assert(mock.power_enable_level == 0);
    assert(mock.close_calls == 1);
    assert(m.gprs_state == GPRS_STATE_NOGPRS);

    printf("test_toggle_sleep_sequence_not_connected OK\n");
}

static void test_toggle_sleep_sequence_while_connected(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);
    m.gprs_state = GPRS_STATE_CONNECTED;

    gprs_modem_toggle(&m, 0);

    int plus_count = 0;
    for (int i = 0; i < mock.write_count; i++) {
        if (mock.writes[i].len == 1 && mock.writes[i].data[0] == '+') plus_count++;
    }
    assert(plus_count == 3);
    assert(writes_contain(&mock, "AT+QICLOSE=0\r"));

    printf("test_toggle_sleep_sequence_while_connected OK\n");
}

static void test_disconnect_sends_three_individual_plus_writes(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);

    gprs_modem_disconnect(&m, 1);

    assert(mock.write_count == 3);
    for (int i = 0; i < 3; i++) {
        assert(mock.writes[i].len == 1);
        assert(mock.writes[i].data[0] == '+');
    }
    assert(m.gprs_state == GPRS_STATE_DISCONNECTED);

    printf("test_disconnect_sends_three_individual_plus_writes OK\n");
}

static void test_disconnect_lan_type_sends_nothing(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);

    gprs_modem_disconnect(&m, 2);

    assert(mock.write_count == 0);
    assert(m.gprs_state == GPRS_STATE_DISCONNECTED);

    printf("test_disconnect_lan_type_sends_nothing OK\n");
}

static void test_set_error_modem_type_power_cycles(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);

    uint32_t result = gprs_modem_set_error(&m, 1, 4242);

    assert(result == 4242);
    assert(m.gprs_state == GPRS_STATE_DISCONNECTED);
    assert(m.gprs_status == GPRS_STATUS_ER3);
    assert(mock.power_enable_level == 0);

    printf("test_set_error_modem_type_power_cycles OK\n");
}

static void test_set_error_lan_type_no_power_cycle(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);

    gprs_modem_set_error(&m, 2, 100);

    assert(m.gprs_state == GPRS_STATE_NOGPRS);
    assert(mock.power_enable_set_count == 0);

    printf("test_set_error_lan_type_no_power_cycle OK\n");
}

static void test_read_response_classification(void) {
    mock_modem_t mock; mock_reset(&mock);
    gprs_transport_t t = make_transport(&mock);
    gprs_modem_t m;
    gprs_modem_init(&m, &t);

    static const uint8_t reply[] = "some OK reply";
    mock.canned_reply = reply;
    mock.canned_reply_len = sizeof(reply) - 1;

    char buf[64];
    gprs_response_result_t r = gprs_modem_read_response(&m, "OK", buf, sizeof(buf), 20);
    assert(r == GPRS_RESP_MATCHED);
    assert(strcmp(buf, "some OK reply") == 0);

    mock.canned_reply = (const uint8_t *)"+CSQ: 12,99";
    mock.canned_reply_len = strlen((const char *)mock.canned_reply);
    r = gprs_modem_read_response(&m, "CSQ:", buf, sizeof(buf), 20);
    assert(r == GPRS_RESP_MATCHED);
    int csq = -1;
    assert(gprs_parse_csq(buf, "CSQ:", &csq) == 1);
    assert(csq == 12);

    printf("test_read_response_classification OK\n");
}

int main(void) {
    test_toggle_wake_sequence();
    test_toggle_sleep_sequence_not_connected();
    test_toggle_sleep_sequence_while_connected();
    test_disconnect_sends_three_individual_plus_writes();
    test_disconnect_lan_type_sends_nothing();
    test_set_error_modem_type_power_cycles();
    test_set_error_lan_type_no_power_cycle();
    test_read_response_classification();
    printf("\nAll gprs_modem tests passed.\n");
    return 0;
}
