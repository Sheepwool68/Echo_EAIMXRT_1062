#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "genie_protocol.h"

static void test_checksum(void) {
    uint8_t data[] = {0x01, 0x02, 0x03};
    assert(genie_checksum(data, 3) == (0x01 ^ 0x02 ^ 0x03));
    assert(genie_checksum(data, 0) == 0);
    printf("test_checksum OK\n");
}

static void test_read_obj_frame(void) {
    uint8_t out[4];
    int n = genie_build_read_obj_frame(GENIE_OBJ_FORM, 0, out);
    assert(n == 4);
    assert(out[0] == GENIE_READ_OBJ);
    assert(out[1] == GENIE_OBJ_FORM);
    assert(out[2] == 0);
    assert(out[3] == (GENIE_READ_OBJ ^ GENIE_OBJ_FORM ^ 0));
    printf("test_read_obj_frame OK\n");
}

static void test_write_obj_frame(void) {
    uint8_t out[6];
    int n = genie_build_write_obj_frame(GENIE_OBJ_LED, 0, 0x0102, out);
    assert(n == 6);
    assert(out[0] == GENIE_WRITE_OBJ);
    assert(out[1] == GENIE_OBJ_LED);
    assert(out[2] == 0);
    assert(out[3] == 0x01);
    assert(out[4] == 0x02);
    assert(out[5] == genie_checksum(out, 5));
    printf("test_write_obj_frame OK\n");
}

static void test_write_contrast_frame_nonzero_adds_2(void) {
    uint8_t out[3];
    int n = genie_build_write_contrast_frame(13, out);
    assert(n == 3);
    assert(out[0] == GENIE_WRITE_CONTRAST);
    assert(out[1] == 15);
    assert(out[2] == genie_checksum(out, 2));
    printf("test_write_contrast_frame_nonzero_adds_2 OK\n");
}

static void test_write_contrast_frame_zero_stays_zero(void) {
    uint8_t out[3];
    int n = genie_build_write_contrast_frame(0, out);
    assert(n == 3);
    assert(out[1] == 0);
    assert(out[2] == genie_checksum(out, 2));
    printf("test_write_contrast_frame_zero_stays_zero OK\n");
}

static void test_write_str_frame(void) {
    uint8_t out[16];
    int n = genie_build_write_str_frame(1, "AB", out, sizeof(out));
    assert(n == 6);
    assert(out[0] == GENIE_WRITE_STR);
    assert(out[1] == 1);
    assert(out[2] == 2);
    assert(out[3] == 'A');
    assert(out[4] == 'B');
    assert(out[5] == genie_checksum(out, 5));
    printf("test_write_str_frame OK\n");
}

static void test_write_str_frame_rejects_output_buffer_too_small(void) {
    uint8_t out[3];
    int n = genie_build_write_str_frame(1, "AB", out, sizeof(out));
    assert(n == -1);
    printf("test_write_str_frame_rejects_output_buffer_too_small OK\n");
}

static void test_write_inh_label_frame(void) {
    uint8_t out[16];
    int n = genie_build_write_inh_label_frame(2, "X", out, sizeof(out));
    assert(n == 5);
    assert(out[0] == GENIE_WRITE_INH_LABEL);
    assert(out[1] == 2);
    assert(out[2] == 1);
    assert(out[3] == 'X');
    assert(out[4] == genie_checksum(out, 4));
    printf("test_write_inh_label_frame OK\n");
}

static void test_magic_bytes_frame(void) {
    uint8_t out[16];
    uint8_t payload[] = {0xAA, 0xBB, 0xCC};
    int n = genie_build_magic_bytes_frame(3, payload, 3, out);
    assert(n == 7); /* 4 + len(3) */
    assert(out[0] == GENIEM_WRITE_BYTES);
    assert(out[1] == 3);
    assert(out[2] == 3);
    assert(out[3] == 0xAA);
    assert(out[4] == 0xBB);
    assert(out[5] == 0xCC);
    assert(out[6] == genie_checksum(out, 6));
    printf("test_magic_bytes_frame OK\n");
}

static void test_magic_dbytes_frame(void) {
    uint8_t out[16];
    uint16_t payload[] = {0x1234, 0x5678};
    int n = genie_build_magic_dbytes_frame(4, payload, 2, out);
    assert(n == 8); /* 4 + 2*len(2) */
    assert(out[0] == GENIEM_WRITE_DBYTES);
    assert(out[1] == 4);
    assert(out[2] == 2);
    assert(out[3] == 0x12 && out[4] == 0x34);
    assert(out[5] == 0x56 && out[6] == 0x78);
    assert(out[7] == genie_checksum(out, 7));
    printf("test_magic_dbytes_frame OK\n");
}

static void test_parse_report_frame_valid(void) {
    uint8_t bytes[GENIE_FRAME_SIZE];
    genie_frame_t f;
    bytes[0] = GENIE_REPORT_EVENT;
    bytes[1] = GENIE_OBJ_FORM;
    bytes[2] = 1;
    bytes[3] = 0;
    bytes[4] = 5;
    bytes[5] = genie_checksum(bytes, 5);

    int ok = genie_parse_report_frame(bytes, &f);
    assert(ok == 1);
    assert(f.cmd == GENIE_REPORT_EVENT);
    assert(f.object == GENIE_OBJ_FORM);
    assert(f.index == 1);
    assert(f.data_msb == 0);
    assert(f.data_lsb == 5);
    printf("test_parse_report_frame_valid OK\n");
}

static void test_parse_report_frame_bad_checksum(void) {
    uint8_t bytes[GENIE_FRAME_SIZE] = {GENIE_REPORT_EVENT, GENIE_OBJ_FORM, 1, 0, 5, 0xFF};
    genie_frame_t f;
    int ok = genie_parse_report_frame(bytes, &f);
    assert(ok == 0);
    printf("test_parse_report_frame_bad_checksum OK\n");
}

static void test_frame_is_and_get_data(void) {
    genie_frame_t f = {GENIE_REPORT_EVENT, GENIE_OBJ_WINBUTTON, 3, 0x01, 0x02};
    assert(genie_frame_is(&f, GENIE_REPORT_EVENT, GENIE_OBJ_WINBUTTON, 3) == 1);
    assert(genie_frame_is(&f, GENIE_REPORT_EVENT, GENIE_OBJ_WINBUTTON, 4) == 0);
    assert(genie_frame_get_data(&f) == 0x0102);
    printf("test_frame_is_and_get_data OK\n");
}

static genie_frame_t make_frame(uint8_t cmd, uint8_t obj, uint8_t idx, uint8_t msb, uint8_t lsb) {
    genie_frame_t f;
    f.cmd = cmd; f.object = obj; f.index = idx; f.data_msb = msb; f.data_lsb = lsb;
    return f;
}

static void test_queue_basic_enqueue_dequeue(void) {
    genie_event_queue_t q;
    genie_frame_t f_in, f_out;
    genie_event_queue_init(&q);

    f_in = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_WINBUTTON, 1, 0, 1);
    int updated = genie_event_queue_enqueue(&q, &f_in);
    assert(updated == 0);
    assert(q.n_events == 1);

    int ok = genie_event_queue_dequeue(&q, &f_out);
    assert(ok == 1);
    assert(f_out.index == 1 && f_out.data_lsb == 1);
    assert(q.n_events == 0);

    printf("test_queue_basic_enqueue_dequeue OK\n");
}

static void test_queue_dequeue_empty(void) {
    genie_event_queue_t q;
    genie_frame_t f_out;
    genie_event_queue_init(&q);
    assert(genie_event_queue_dequeue(&q, &f_out) == 0);
    printf("test_queue_dequeue_empty OK\n");
}

static void test_queue_dedup_coalesces_same_object(void) {
    genie_event_queue_t q;
    genie_frame_t f1, f2, f_out;
    genie_event_queue_init(&q);

    f1 = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_TRACKBAR, 5, 0, 10);
    f2 = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_TRACKBAR, 5, 0, 20);

    genie_event_queue_enqueue(&q, &f1);
    int updated = genie_event_queue_enqueue(&q, &f2);

    assert(updated == 1);
    assert(q.n_events == 1);

    genie_event_queue_dequeue(&q, &f_out);
    assert(f_out.data_lsb == 20);

    printf("test_queue_dedup_coalesces_same_object OK\n");
}

static void test_queue_different_index_not_coalesced(void) {
    genie_event_queue_t q;
    genie_frame_t f1, f2;
    genie_event_queue_init(&q);

    f1 = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_TRACKBAR, 5, 0, 10);
    f2 = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_TRACKBAR, 6, 0, 20);

    genie_event_queue_enqueue(&q, &f1);
    int updated = genie_event_queue_enqueue(&q, &f2);

    assert(updated == 0);
    assert(q.n_events == 2);

    printf("test_queue_different_index_not_coalesced OK\n");
}

static void test_queue_capacity_margin_bailout(void) {
    genie_event_queue_t q;
    genie_frame_t f;
    int i;
    genie_event_queue_init(&q);

    for (i = 0; i < MAX_GENIE_EVENTS - 2; i++) {
        f = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_WINBUTTON, (uint8_t)i, 0, 0);
        genie_event_queue_enqueue(&q, &f);
    }
    assert(q.n_events == MAX_GENIE_EVENTS - 2);

    f = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_WINBUTTON, 99, 0, 0);
    int updated = genie_event_queue_enqueue(&q, &f);
    assert(updated == 0);
    assert(q.n_events == MAX_GENIE_EVENTS - 2);

    printf("test_queue_capacity_margin_bailout OK\n");
}

static void test_queue_ring_buffer_wraparound(void) {
    genie_event_queue_t q;
    genie_frame_t f, f_out;
    int i;
    genie_event_queue_init(&q);

    for (i = 0; i < MAX_GENIE_EVENTS * 3; i++) {
        f = make_frame(GENIE_REPORT_EVENT, GENIE_OBJ_WINBUTTON, (uint8_t)(i % 100), 0, (uint8_t)i);
        genie_event_queue_enqueue(&q, &f);
        genie_event_queue_dequeue(&q, &f_out);
        assert(f_out.data_lsb == (uint8_t)i);
    }
    assert(q.n_events == 0);

    printf("test_queue_ring_buffer_wraparound OK\n");
}

int main(void) {
    test_checksum();
    test_read_obj_frame();
    test_write_obj_frame();
    test_write_contrast_frame_nonzero_adds_2();
    test_write_contrast_frame_zero_stays_zero();
    test_write_str_frame();
    test_write_str_frame_rejects_output_buffer_too_small();
    test_write_inh_label_frame();
    test_magic_bytes_frame();
    test_magic_dbytes_frame();
    test_parse_report_frame_valid();
    test_parse_report_frame_bad_checksum();
    test_frame_is_and_get_data();
    test_queue_basic_enqueue_dequeue();
    test_queue_dequeue_empty();
    test_queue_dedup_coalesces_same_object();
    test_queue_different_index_not_coalesced();
    test_queue_capacity_margin_bailout();
    test_queue_ring_buffer_wraparound();
    printf("\nAll genie_protocol tests passed.\n");
    return 0;
}
