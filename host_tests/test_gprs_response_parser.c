#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "gprs_response_parser.h"

static void test_classify_response(void) {
    assert(gprs_classify_response("", "OK") == GPRS_RESP_NONE);
    assert(gprs_classify_response("some junk", "OK") == GPRS_RESP_UNMATCHED);
    assert(gprs_classify_response("prefix OK suffix", "OK") == GPRS_RESP_MATCHED);
    printf("test_classify_response OK\n");
}

static void test_parse_csq_valid(void) {
    int csq = -1;
    int ok = gprs_parse_csq("+CSQ: 15,99", "CSQ:", &csq);
    assert(ok == 1);
    assert(csq == 15);
    printf("test_parse_csq_valid OK (csq=%d)\n", csq);
}

static void test_parse_csq_out_of_range(void) {
    int csq = -1;
    int ok = gprs_parse_csq("+CSQ: 99,99", "CSQ:", &csq);
    assert(ok == 0);
    printf("test_parse_csq_out_of_range OK\n");
}

static void test_parse_csq_marker_not_found(void) {
    int csq = -1;
    int ok = gprs_parse_csq("ERROR", "CSQ:", &csq);
    assert(ok == 0);
    printf("test_parse_csq_marker_not_found OK\n");
}

static void test_parse_csq_zero_is_invalid(void) {
    int csq = -1;
    int ok = gprs_parse_csq("+CSQ: 00,99", "CSQ:", &csq);
    assert(ok == 0);
    printf("test_parse_csq_zero_is_invalid OK\n");
}

#define MAX_EVENTS 16
static rcfg_event_t g_events[MAX_EVENTS];
static int g_event_count;

static void rcfg_collect(void *ctx, const rcfg_event_t *ev) {
    (void)ctx;
    if (g_event_count < MAX_EVENTS) g_events[g_event_count++] = *ev;
}
static void rcfg_reset(void) { g_event_count = 0; }

static void test_rcfg_beeper_applied(void) {
    uint8_t buf[8] = {0x03, 0x00, 0x08, 0x21, 0x01, 0,0,0};
    rcfg_reset();
    int mark = -1;
    int ret = rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, &mark);
    assert(ret == 8);
    assert(mark == 0);
    assert(g_event_count == 1);
    assert(g_events[0].type == RCFG_EVT_BEEPER_SET);
    assert(g_events[0].beeper_on == 1);
    printf("test_rcfg_beeper_applied OK\n");
}

static void test_rcfg_beeper_off(void) {
    uint8_t buf[8] = {0x03, 0x00, 0x08, 0x21, 0x00, 0,0,0};
    rcfg_reset();
    rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, NULL);
    assert(g_events[0].beeper_on == 0);
    printf("test_rcfg_beeper_off OK\n");
}

static void test_rcfg_send_to_remote_applied(void) {
    uint8_t buf[8] = {0x03, 0x00, 0x08, 0x2E, 0x01, 0,0,0};
    rcfg_reset();
    rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, NULL);
    assert(g_event_count == 1);
    assert(g_events[0].type == RCFG_EVT_SEND_TO_REMOTE_SET);
    assert(g_events[0].send_to_remote_value == 1);
    printf("test_rcfg_send_to_remote_applied OK\n");
}

static void test_rcfg_stop_and_start(void) {
    uint8_t stop_buf[8] = {0x03, 0x00, 0x07, 0x53, 0,0,0,0};
    rcfg_reset();
    rcfg_process_buffer(stop_buf, sizeof(stop_buf), rcfg_collect, NULL, NULL);
    assert(g_event_count == 1);
    assert(g_events[0].type == RCFG_EVT_STOP_READING);

    uint8_t start_buf[8] = {0x03, 0x00, 0x07, 0x52, 0,0,0,0};
    rcfg_reset();
    rcfg_process_buffer(start_buf, sizeof(start_buf), rcfg_collect, NULL, NULL);
    assert(g_event_count == 1);
    assert(g_events[0].type == RCFG_EVT_START_READING);

    printf("test_rcfg_stop_and_start OK\n");
}

static void test_rcfg_noop_commands_consumed_correctly(void) {
    uint8_t buf[8] = {0x03, 0x00, 0x09, 0x1E, 0x00, 0x0A, 0,0};
    rcfg_reset();
    int ret = rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, NULL);
    assert(ret == 9);
    assert(g_event_count == 1);
    assert(g_events[0].type == RCFG_EVT_NOOP);
    assert(g_events[0].noop_command_byte == 0x1E);
    printf("test_rcfg_noop_commands_consumed_correctly OK\n");
}

static void test_rcfg_rewind_remote_vs_local(void) {
    uint8_t buf[10] = {0x03, 0x00, 0x0C, 0x39, 'A','B','1','2','3', 0x03};
    rcfg_reset();
    int mark = -1;
    int ret = rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, &mark);
    assert(ret == 12);
    assert(g_event_count == 1);
    assert(g_events[0].type == RCFG_EVT_REWIND);
    assert(g_events[0].is_remote_rewind == 1);
    assert(g_events[0].rewind_data == &buf[3]);
    assert(g_events[0].rewind_data_len == sizeof(buf) - 3);

    uint8_t buf_local[10] = {0x03, 0x00, 0x0C, 0x38, 'A','B','1','2','3', 0x03};
    rcfg_reset();
    rcfg_process_buffer(buf_local, sizeof(buf_local), rcfg_collect, NULL, NULL);
    assert(g_events[0].is_remote_rewind == 0);

    printf("test_rcfg_rewind_remote_vs_local OK\n");
}

static void test_rcfg_heartbeat_ilen6_no_commands(void) {
    uint8_t buf[9] = {0x03, 0x00, 0x06, 0,0,0,0,0,0};
    rcfg_reset();
    int ret = rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, NULL);
    assert(ret == 6);
    assert(g_event_count == 0);
    printf("test_rcfg_heartbeat_ilen6_no_commands OK\n");
}

static void test_rcfg_ilen0_marks_client_outreach(void) {
    uint8_t buf[3] = {0x03, 0x00, 0x00};
    rcfg_reset();
    int mark = 0;
    int ret = rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, &mark);
    assert(ret == 0);
    assert(mark == 1);
    assert(g_event_count == 0);
    printf("test_rcfg_ilen0_marks_client_outreach OK\n");
}

static void test_rcfg_unrecognized_byte_stops_loop(void) {
    uint8_t buf[8] = {0x03, 0x00, 0x08, 0xFE, 0,0,0,0};
    rcfg_reset();
    rcfg_process_buffer(buf, sizeof(buf), rcfg_collect, NULL, NULL);
    assert(g_event_count == 0);
    printf("test_rcfg_unrecognized_byte_stops_loop OK\n");
}

#define MAX_RX_EVENTS 16
static gprs_rx_event_t g_rx_events[MAX_RX_EVENTS];
static int g_rx_event_count;

static void rx_collect(void *ctx, const gprs_rx_event_t *ev) {
    (void)ctx;
    if (g_rx_event_count < MAX_RX_EVENTS) g_rx_events[g_rx_event_count++] = *ev;
}
static void rx_reset(void) { g_rx_event_count = 0; }

static void test_rx_config_record_detected(void) {
    uint8_t buf[8] = {0x03, 0x00, 0x00, 'X','X','X','X','X'};
    rx_reset();
    gprs_process_response_buffer(buf, sizeof(buf), rx_collect, NULL);
    assert(g_rx_event_count >= 1);
    assert(g_rx_events[0].type == GPRS_RX_CONFIG_RECORD);
    assert(g_rx_events[0].config_record_data == &buf[0]);
    printf("test_rx_config_record_detected OK\n");
}

static void test_rx_ok_ack_valid(void) {
    uint8_t buf[] = "OK123";
    rx_reset();
    gprs_process_response_buffer(buf, strlen((char*)buf), rx_collect, NULL);
    assert(g_rx_event_count == 1);
    assert(g_rx_events[0].type == GPRS_RX_OK_ACK);
    assert(g_rx_events[0].ack_valid == 1);
    assert(g_rx_events[0].ack_record_no == 124);
    printf("test_rx_ok_ack_valid OK\n");
}

static void test_rx_ok_ack_invalid(void) {
    uint8_t buf[] = "OK0xyz";
    rx_reset();
    gprs_process_response_buffer(buf, strlen((char*)buf), rx_collect, NULL);
    assert(g_rx_event_count == 1);
    assert(g_rx_events[0].type == GPRS_RX_OK_ACK);
    assert(g_rx_events[0].ack_valid == 0);
    printf("test_rx_ok_ack_invalid OK\n");
}

static void test_rx_long_config_record_does_not_corrupt_next_command(void) {
    uint8_t buf[16] = {
        0x03, 0x00, 0x0A,
        'O','K','1','2','3','4','5','6','7','8',
        0x4F, 0x4B
    };
    rx_reset();
    gprs_process_response_buffer(buf, sizeof(buf), rx_collect, NULL);

    int found_real_ack = 0;
    for (int i = 0; i < g_rx_event_count; i++) {
        if (g_rx_events[i].type == GPRS_RX_OK_ACK) {
            found_real_ack = 1;
        }
    }
    assert(g_rx_events[0].type == GPRS_RX_CONFIG_RECORD);
    assert(found_real_ack == 1);
    printf("test_rx_long_config_record_does_not_corrupt_next_command OK\n");
}

int main(void) {
    test_classify_response();
    test_parse_csq_valid();
    test_parse_csq_out_of_range();
    test_parse_csq_marker_not_found();
    test_parse_csq_zero_is_invalid();
    test_rcfg_beeper_applied();
    test_rcfg_beeper_off();
    test_rcfg_send_to_remote_applied();
    test_rcfg_stop_and_start();
    test_rcfg_noop_commands_consumed_correctly();
    test_rcfg_rewind_remote_vs_local();
    test_rcfg_heartbeat_ilen6_no_commands();
    test_rcfg_ilen0_marks_client_outreach();
    test_rcfg_unrecognized_byte_stops_loop();
    test_rx_config_record_detected();
    test_rx_ok_ack_valid();
    test_rx_ok_ack_invalid();
    test_rx_long_config_record_does_not_corrupt_next_command();
    printf("\nAll gprs_response_parser tests passed.\n");
    return 0;
}
