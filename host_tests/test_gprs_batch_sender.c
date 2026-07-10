#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "gprs_batch_sender.h"

#define MAX_MOCK_RECORDS 250
typedef struct {
    nrf_record_t records[MAX_MOCK_RECORDS];
    int count;
    uint64_t seek_pos;
    int seek_calls;
} mock_source_t;

static int src_seek(void *ctx, uint64_t idx) {
    mock_source_t *s = (mock_source_t *)ctx;
    s->seek_pos = idx;
    s->seek_calls++;
    return 0;
}
static int src_read(void *ctx, nrf_record_t *out) {
    mock_source_t *s = (mock_source_t *)ctx;
    if (s->seek_pos >= (uint64_t)s->count) {
        return 0;
    }
    *out = s->records[s->seek_pos];
    s->seek_pos++;
    return 1;
}

typedef struct {
    uint8_t last_written[128];
    size_t last_len;
    int write_count;
    int force_fail_after;
} mock_sink_t;

static int sink_write(void *ctx, const uint8_t *buf, size_t len) {
    mock_sink_t *s = (mock_sink_t *)ctx;
    if (s->force_fail_after >= 0 && s->write_count >= s->force_fail_after) {
        return -1;
    }
    memcpy(s->last_written, buf, len);
    s->last_len = len;
    s->write_count++;
    return (int)len;
}

static void fill_mock_records(mock_source_t *src, int count) {
    memset(src, 0, sizeof(*src));
    src->count = count;
    for (int i = 0; i < count; i++) {
        memset(&src->records[i], 0, sizeof(nrf_record_t));
        src->records[i].xpdr_code[0] = 0;
        src->records[i].xpdr_code[2] = (char)i;
        src->records[i].date_time = 1000 + (uint32_t)i;
    }
}

static gprs_batch_sender_config_t make_config(mock_source_t *src, mock_sink_t *sink) {
    gprs_batch_sender_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.source_ctx = src;
    cfg.seek = src_seek;
    cfg.read = src_read;
    cfg.sink_ctx = sink;
    cfg.sink = sink_write;
    cfg.channel = 3;
    for (int i = 0; i < 6; i++) cfg.mac_address[i] = (uint8_t)(0x20 + i);
    return cfg;
}

static void test_sends_all_available_when_fewer_than_batch_size(void) {
    mock_source_t src; mock_sink_t sink;
    fill_mock_records(&src, 5);
    memset(&sink, 0, sizeof(sink)); sink.force_fail_after = -1;
    gprs_batch_sender_config_t cfg = make_config(&src, &sink);

    uint32_t current_rec = 0;
    int sent = gprs_send_next_batch(&cfg, &current_rec);

    assert(sent == 5);
    assert(current_rec == 5);
    assert(sink.write_count == 5);
    assert(src.seek_calls == 1);
    assert(src.seek_pos == 5);

    printf("test_sends_all_available_when_fewer_than_batch_size OK\n");
}

static void test_caps_at_records_per_batch(void) {
    mock_source_t src; mock_sink_t sink;
    fill_mock_records(&src, 200);
    memset(&sink, 0, sizeof(sink)); sink.force_fail_after = -1;
    gprs_batch_sender_config_t cfg = make_config(&src, &sink);

    uint32_t current_rec = 0;
    int sent = gprs_send_next_batch(&cfg, &current_rec);

    assert(sent == GPRS_RECORDS_PER_BATCH);
    assert(current_rec == GPRS_RECORDS_PER_BATCH);

    int sent2 = gprs_send_next_batch(&cfg, &current_rec);
    assert(sent2 == GPRS_RECORDS_PER_BATCH);
    assert(current_rec == GPRS_RECORDS_PER_BATCH * 2);

    printf("test_caps_at_records_per_batch OK\n");
}

static void test_zero_records_available(void) {
    mock_source_t src; mock_sink_t sink;
    fill_mock_records(&src, 0);
    memset(&sink, 0, sizeof(sink)); sink.force_fail_after = -1;
    gprs_batch_sender_config_t cfg = make_config(&src, &sink);

    uint32_t current_rec = 10;
    int sent = gprs_send_next_batch(&cfg, &current_rec);

    assert(sent == 0);
    assert(current_rec == 10);

    printf("test_zero_records_available OK\n");
}

static void test_write_failure_stops_batch_without_advancing_failed_record(void) {
    mock_source_t src; mock_sink_t sink;
    fill_mock_records(&src, 10);
    memset(&sink, 0, sizeof(sink));
    sink.force_fail_after = 3;
    gprs_batch_sender_config_t cfg = make_config(&src, &sink);

    uint32_t current_rec = 0;
    int sent = gprs_send_next_batch(&cfg, &current_rec);

    assert(sent == -1);
    assert(current_rec == 3);

    printf("test_write_failure_stops_batch_without_advancing_failed_record OK\n");
}

static void test_resumes_from_current_rec_on_next_call(void) {
    mock_source_t src; mock_sink_t sink;
    fill_mock_records(&src, 10);
    memset(&sink, 0, sizeof(sink)); sink.force_fail_after = -1;
    gprs_batch_sender_config_t cfg = make_config(&src, &sink);

    uint32_t current_rec = 4;
    int sent = gprs_send_next_batch(&cfg, &current_rec);

    assert(sent == 6);
    assert(current_rec == 10);
    assert(src.seek_calls == 1);

    printf("test_resumes_from_current_rec_on_next_call OK\n");
}

int main(void) {
    test_sends_all_available_when_fewer_than_batch_size();
    test_caps_at_records_per_batch();
    test_zero_records_available();
    test_write_failure_stops_batch_without_advancing_failed_record();
    test_resumes_from_current_rec_on_next_call();
    printf("\nAll gprs_batch_sender tests passed.\n");
    return 0;
}
