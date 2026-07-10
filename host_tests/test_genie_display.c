#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "genie_display.h"

static uint32_t g_fake_now;
static uint32_t g_fake_step = 10;
uint32_t systick_ms_now(void) {
    uint32_t v = g_fake_now;
    g_fake_now += g_fake_step;
    return v;
}
static void reset_fake_clock(void) { g_fake_now = 100000; g_fake_step = 10; }

typedef struct {
    uint8_t rx_buf[512];
    size_t rx_len, rx_pos;
    uint8_t tx_buf[512];
    size_t tx_len;
} mock_serial_t;

static void mock_reset(mock_serial_t *m) { memset(m, 0, sizeof(*m)); }
static void mock_push_rx(mock_serial_t *m, const uint8_t *data, size_t len) {
    memcpy(&m->rx_buf[m->rx_len], data, len);
    m->rx_len += len;
}
static int m_read_available(void *ctx) {
    mock_serial_t *m = (mock_serial_t *)ctx;
    return (int)(m->rx_len - m->rx_pos);
}
static int m_peek(void *ctx) {
    mock_serial_t *m = (mock_serial_t *)ctx;
    if (m->rx_pos >= m->rx_len) return -1;
    return m->rx_buf[m->rx_pos];
}
static int m_getc(void *ctx) {
    mock_serial_t *m = (mock_serial_t *)ctx;
    return m->rx_buf[m->rx_pos++];
}
static void m_write(void *ctx, const uint8_t *buf, size_t len) {
    mock_serial_t *m = (mock_serial_t *)ctx;
    memcpy(&m->tx_buf[m->tx_len], buf, len);
    m->tx_len += len;
}
static void m_flush_rx(void *ctx) { mock_serial_t *m = (mock_serial_t *)ctx; m->rx_pos = m->rx_len = 0; }
static void m_flush_tx(void *ctx) { mock_serial_t *m = (mock_serial_t *)ctx; m->tx_len = 0; }

static genie_transport_t make_transport(mock_serial_t *m) {
    genie_transport_t t;
    t.ctx = m;
    t.write = m_write;
    t.read_available = m_read_available;
    t.peek = m_peek;
    t.getc = m_getc;
    t.flush_rx = m_flush_rx;
    t.flush_tx = m_flush_tx;
    return t;
}

static void push_report_frame(mock_serial_t *m, uint8_t cmd, uint8_t object, uint8_t index,
                               uint8_t data_msb, uint8_t data_lsb) {
    uint8_t bytes[6];
    bytes[0] = cmd; bytes[1] = object; bytes[2] = index; bytes[3] = data_msb; bytes[4] = data_lsb;
    bytes[5] = genie_checksum(bytes, 5);
    mock_push_rx(m, bytes, 6);
}

static void test_genie_begin_success(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;

    reset_fake_clock();
    push_report_frame(&m, GENIE_REPORT_OBJ, GENIE_OBJ_FORM, 0, 0, 0);

    int ok = genie_begin(&g, &t);
    assert(ok == 1);
    assert(genie_online(&g) == 1);

    printf("test_genie_begin_success OK\n");
}

static void test_genie_begin_timeout(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;

    reset_fake_clock();
    int ok = genie_begin(&g, &t);
    assert(ok == 0);
    assert(genie_online(&g) == 0);

    printf("test_genie_begin_timeout OK\n");
}

static void test_write_object_ack(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    g.genie_cmd_timeout = 1250;

    {
        uint8_t b = GENIE_ACK;
        mock_push_rx(&m, &b, 1);
    }

    int r = genie_write_object(&g, GENIE_OBJ_LED, 0, 1);
    assert(r == 1);
    assert(g.pending_ack == 0);

    printf("test_write_object_ack OK\n");
}

static void test_write_object_nak(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    g.genie_cmd_timeout = 1250;

    {
        uint8_t b = GENIE_NAK;
        mock_push_rx(&m, &b, 1);
    }

    int r = genie_write_object(&g, GENIE_OBJ_LED, 0, 1);
    assert(r == 0);

    printf("test_write_object_nak OK\n");
}

static void test_write_object_timeout_sets_manual_disconnect(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    g_fake_step = 100;
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    g.genie_cmd_timeout = 1250;

    int r = genie_write_object(&g, GENIE_OBJ_LED, 0, 1);
    assert(r == -1);
    assert(g.display_detect_timer > 0);

    printf("test_write_object_timeout_sets_manual_disconnect OK\n");
}

static void test_report_event_sets_current_form_from_index(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    genie_event_queue_init(&g.event_queue);

    push_report_frame(&m, GENIE_REPORT_EVENT, GENIE_OBJ_FORM, 5, 0, 99);

    int r = genie_do_events(&g);
    assert(r == GENIE_REPORT_EVENT);
    assert(g.current_form == 5);
    assert(g.event_queue.n_events == 1);

    printf("test_report_event_sets_current_form_from_index OK\n");
}

static void test_report_obj_sets_current_form_from_data_lsb(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    g.auto_ping_timer = g_fake_now; /* prevent genie_ping()'s own firing
        (called first thing inside genie_do_events, same as the
        original) from setting auto_ping=1 and causing this frame to be
        treated as a ping reply instead of the real event this test
        means to check */
    genie_event_queue_init(&g.event_queue);

    push_report_frame(&m, GENIE_REPORT_OBJ, GENIE_OBJ_FORM, 42, 0, 7);

    int r = genie_do_events(&g);
    assert(r == GENIE_REPORT_OBJ);
    assert(g.current_form == 7);
    assert(g.event_queue.n_events == 1);

    printf("test_report_obj_sets_current_form_from_data_lsb OK\n");
}

static void test_bad_checksum_discarded(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    g.current_form = -1;
    genie_event_queue_init(&g.event_queue);

    uint8_t bad_frame[6] = {GENIE_REPORT_EVENT, GENIE_OBJ_FORM, 5, 0, 0, 0xFF};
    mock_push_rx(&m, bad_frame, 6);

    int r = genie_do_events(&g);
    assert(r == 0);
    assert(g.current_form == -1);
    assert(g.event_queue.n_events == 0);

    printf("test_bad_checksum_discarded OK\n");
}

static void test_partial_frame_waits_for_more_bytes(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    genie_event_queue_init(&g.event_queue);

    uint8_t partial[3] = {GENIE_REPORT_EVENT, GENIE_OBJ_FORM, 5};
    mock_push_rx(&m, partial, 3);

    int r = genie_do_events(&g);
    assert(r == 0);
    assert(m.rx_pos == 0);

    printf("test_partial_frame_waits_for_more_bytes OK\n");
}

static void test_unknown_byte_counts_toward_manual_disconnect(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    int i;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;

    for (i = 0; i < 11; i++) {
        uint8_t b = 0x42;
        mock_reset(&m);
        mock_push_rx(&m, &b, 1);
        int r = genie_do_events(&g);
        assert(r == GENIE_NAK);
    }
    assert(g.bad_byte_counter == 0);
    assert(g.display_detect_timer > 0);

    printf("test_unknown_byte_counts_toward_manual_disconnect OK\n");
}

static void test_nak_counter_resets_on_second_consecutive_call(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    g.auto_ping_timer = g_fake_now; /* prevent genie_ping's own read-form
        request from also landing in tx_buf and confusing the
        tx_len assertions below */

    {
        uint8_t b = GENIE_NAK;
        mock_push_rx(&m, &b, 1);
    }
    genie_do_events(&g);
    assert(g.nak_inj == 1);
    assert(m.tx_len == 0);

    mock_reset(&m);
    g.auto_ping_timer = g_fake_now;
    {
        uint8_t b = GENIE_NAK;
        mock_push_rx(&m, &b, 1);
    }
    genie_do_events(&g);
    assert(g.nak_inj == 0);
    assert(m.tx_len == 1 && m.tx_buf[0] == 0xFF);

    printf("test_nak_counter_resets_on_second_consecutive_call OK\n");
}

static void test_ping_reply_filtered_and_marks_connected(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 0;
    g.recover_pulse = 50;
    genie_event_queue_init(&g.event_queue);

    push_report_frame(&m, GENIE_REPORT_OBJ, GENIE_OBJ_FORM, 0, 0, 3);

    int r = genie_do_events(&g);

    assert(g.display_detected == 1);
    assert(g.auto_ping == 0);
    assert(g.event_queue.n_events == 1);
    {
        genie_frame_t f;
        genie_event_queue_dequeue(&g.event_queue, &f);
        assert(f.cmd == GENIE_PING);
        assert(f.object == GENIE_READY);
    }
    (void)r;

    printf("test_ping_reply_filtered_and_marks_connected OK\n");
}

static uint8_t g_magic_handler_index, g_magic_handler_len;
static int g_magic_handler_calls;
static void magic_byte_handler(void *ctx, uint8_t index, uint8_t len) {
    (void)ctx;
    g_magic_handler_calls++;
    g_magic_handler_index = index;
    g_magic_handler_len = len;
}

static void test_magic_bytes_dispatches_to_handler(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    genie_event_queue_init(&g.event_queue);
    g_magic_handler_calls = 0;
    genie_attach_magic_byte_reader(&g, NULL, magic_byte_handler);

    uint8_t frame[] = {GENIEM_REPORT_BYTES, 7, 3, 0xEE};
    mock_push_rx(&m, frame, sizeof(frame));

    int r = genie_do_events(&g);
    assert(r == GENIEM_REPORT_BYTES);
    assert(g_magic_handler_calls == 1);
    assert(g_magic_handler_index == 7);
    assert(g_magic_handler_len == 3);

    printf("test_magic_bytes_dispatches_to_handler OK\n");
}

static void test_magic_bytes_no_handler_discards_payload(void) {
    mock_serial_t m; mock_reset(&m);
    genie_transport_t t = make_transport(&m);
    genie_display_t g;
    reset_fake_clock();
    memset(&g, 0, sizeof(g));
    g.transport = &t;
    g.display_detected = 1;
    genie_event_queue_init(&g.event_queue);

    uint8_t frame[] = {GENIEM_REPORT_BYTES, 7, 3, 0xAA, 0xBB, 0xCC, 0xDD};
    mock_push_rx(&m, frame, sizeof(frame));

    int r = genie_do_events(&g);
    assert(r == GENIEM_REPORT_BYTES);
    assert(m.rx_pos == m.rx_len);

    printf("test_magic_bytes_no_handler_discards_payload OK\n");
}

int main(void) {
    test_genie_begin_success();
    test_genie_begin_timeout();
    test_write_object_ack();
    test_write_object_nak();
    test_write_object_timeout_sets_manual_disconnect();
    test_report_event_sets_current_form_from_index();
    test_report_obj_sets_current_form_from_data_lsb();
    test_bad_checksum_discarded();
    test_partial_frame_waits_for_more_bytes();
    test_unknown_byte_counts_toward_manual_disconnect();
    test_nak_counter_resets_on_second_consecutive_call();
    test_ping_reply_filtered_and_marks_connected();
    test_magic_bytes_dispatches_to_handler();
    test_magic_bytes_no_handler_discards_payload();
    printf("\nAll genie_display tests passed.\n");
    return 0;
}
