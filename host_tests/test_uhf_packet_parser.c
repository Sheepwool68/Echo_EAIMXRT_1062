#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "uhf_packet_parser.h"
#include "uhf_crc.h"

#define MAX_EVENTS 16
static uhf_frame_event_t g_events[MAX_EVENTS];
static int g_event_count;

static void collect_cb(void *ctx, const uhf_frame_event_t *ev) {
    (void)ctx;
    if (g_event_count < MAX_EVENTS) {
        g_events[g_event_count++] = *ev;
    }
}

static void reset_events(void) { g_event_count = 0; }

static void test_heartbeat_frame(void) {
    uint8_t buf[13];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x06; buf[2] = 0xAA;

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(consumed == 13);
    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_HEARTBEAT);
    printf("test_heartbeat_frame OK\n");
}

static void test_end_of_round_frame(void) {
    uint8_t buf[29];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x16; buf[2] = 0xAA; buf[21] = 0x05;

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(consumed == 29);
    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_END_OF_ROUND);
    printf("test_end_of_round_frame OK\n");
}

static void test_start_confirm_frame(void) {
    uint8_t buf[19];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x0c; buf[2] = 0xAA;

    reset_events();
    uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_START_CONFIRM);
    printf("test_start_confirm_frame OK\n");
}

static void test_temperature_frame(void) {
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x00; buf[2] = 0x72; buf[5] = 42;

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(consumed == 8);
    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_TEMPERATURE);
    assert(g_events[0].data.temperature == 42);
    printf("test_temperature_frame OK\n");
}

static void test_ant_status_frame(void) {
    /* "FF 09 61 00 00 05 01 00 02 00 03 00 04 01" from the doc comment */
    uint8_t buf[14] = {0xFF,0x09,0x61,0x00,0x00,0x05,0x01,0x00,0x02,0x00,0x03,0x00,0x04,0x01};

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(consumed == 14);
    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_ANT_STATUS);
    /* traced by hand in the porting notes: buf[7]=0,buf[9]=0,buf[11]=0,buf[13]=1 -> mask 0b0001 */
    assert(g_events[0].data.ant_status_mask == 0x01);
    printf("test_ant_status_frame OK (mask=0x%02X)\n", g_events[0].data.ant_status_mask);
}

static void test_return_loss_frame_good(void) {
    uint8_t buf[28];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x15; buf[2] = 0xAA;
    buf[19] = 2;   /* antenna 2 */
    buf[25] = 150; /* >= 100 -> good */

    reset_events();
    uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_RETURN_LOSS);
    assert(g_events[0].data.return_loss.antenna == 2);
    assert(g_events[0].data.return_loss.good == 1);
    assert(g_events[0].data.return_loss.ant_bit == (8 >> 1)); /* ant 2 -> bit2 */
    printf("test_return_loss_frame_good OK\n");
}

static void test_return_loss_frame_bad(void) {
    uint8_t buf[28];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x15; buf[2] = 0xAA;
    buf[19] = 1;
    buf[25] = 50; /* < 100 -> bad */

    reset_events();
    uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(g_event_count == 1);
    assert(g_events[0].data.return_loss.good == 0);
    assert(g_events[0].data.return_loss.percent == 0); /* original's explicit override */
    printf("test_return_loss_frame_bad OK\n");
}

static void build_tag_frame(uint8_t *buf, size_t buf_size, uint8_t datalength,
                             uint8_t reads, uint8_t rssi_raw, uint8_t ant_no,
                             uint32_t chip_code)
{
    memset(buf, 0, buf_size);
    buf[0] = 0xFF;
    buf[1] = datalength;
    buf[2] = 0xAA; /* must NOT match any special b1 value (0x06/0x16/0x0c/0x15) */
    buf[7] = reads;
    buf[8] = rssi_raw;
    buf[9] = ant_no;
    buf[22] = 0x30; /* top 5 bits = 6 -> pcwordbytes = 12 -> chip_offset = 32 */
    buf[32] = (uint8_t)(chip_code >> 24);
    buf[33] = (uint8_t)(chip_code >> 16);
    buf[34] = (uint8_t)(chip_code >> 8);
    buf[35] = (uint8_t)(chip_code);
    uhf_add_crc(buf, (size_t)datalength + 5);
}

static void test_tag_read_frame(void) {
    uint8_t buf[50];
    build_tag_frame(buf, sizeof(buf), 40, 3, 0xCE /* -50 dBm */, 2, 0xDEADBEEF);

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(consumed == (size_t)40 + 7);
    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_TAG_READ);
    assert(g_events[0].data.tag.chip_code == 0xDEADBEEF);
    assert(g_events[0].data.tag.rssi == -50);
    assert(g_events[0].data.tag.antenna == 2);
    assert(g_events[0].data.tag.reads == 3);
    /* rssi_percent = -50*1.7+160 = 75 */
    assert(g_events[0].data.tag.rssi_percent == 75);
    printf("test_tag_read_frame OK\n");
}

static void test_tag_read_bad_crc_rejected(void) {
    uint8_t buf[50];
    build_tag_frame(buf, sizeof(buf), 40, 3, 0xCE, 2, 0xDEADBEEF);
    buf[10] ^= 0xFF; /* corrupt a payload byte after CRC was computed */

    reset_events();
    uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    /* frame is still consumed (advances past its claimed length) but no
     * tag-read event is produced, matching the original silently
     * dropping a corrupt frame rather than misreporting a chip code */
    assert(g_event_count == 0);
    printf("test_tag_read_bad_crc_rejected OK\n");
}

static void test_insufficient_data_stops_whole_scan(void) {
    /* A frame that looks like it might be a tag read (b1 not matching
     * any special value) but with fewer than 40 bytes remaining --
     * original bails out of the ENTIRE scan, not just this frame. */
    uint8_t buf[20];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x20; buf[2] = 0xAA; /* b1=0x20, not a special value */

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(g_event_count == 0);
    assert(consumed == 0); /* stopped at the very start, i=0, since len-i=20 < 40 */
    printf("test_insufficient_data_stops_whole_scan OK\n");
}

static void test_multiple_frames_in_one_buffer(void) {
    uint8_t buf[13 + 19]; /* heartbeat + start_confirm back to back */
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x06; buf[2] = 0xAA; /* heartbeat, 13 bytes */
    buf[13] = 0xFF; buf[14] = 0x0c; buf[15] = 0xAA; /* start_confirm, 19 bytes */

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(consumed == sizeof(buf));
    assert(g_event_count == 2);
    assert(g_events[0].type == UHF_FRAME_HEARTBEAT);
    assert(g_events[1].type == UHF_FRAME_START_CONFIRM);
    printf("test_multiple_frames_in_one_buffer OK\n");
}

static void test_resync_skips_garbage_bytes(void) {
    uint8_t buf[3 + 13];
    buf[0] = 0x11; buf[1] = 0x22; buf[2] = 0x33; /* garbage, no 0xFF */
    buf[3] = 0xFF; buf[4] = 0x06; buf[5] = 0xAA; /* heartbeat starts here */
    memset(&buf[6], 0, sizeof(buf) - 6);

    reset_events();
    size_t consumed = uhf_process_buffer(buf, sizeof(buf), collect_cb, NULL);

    assert(consumed == sizeof(buf));
    assert(g_event_count == 1);
    assert(g_events[0].type == UHF_FRAME_HEARTBEAT);
    printf("test_resync_skips_garbage_bytes OK\n");
}

int main(void) {
    test_heartbeat_frame();
    test_end_of_round_frame();
    test_start_confirm_frame();
    test_temperature_frame();
    test_ant_status_frame();
    test_return_loss_frame_good();
    test_return_loss_frame_bad();
    test_tag_read_frame();
    test_tag_read_bad_crc_rejected();
    test_insufficient_data_stops_whole_scan();
    test_multiple_frames_in_one_buffer();
    test_resync_skips_garbage_bytes();
    printf("\nAll uhf_packet_parser tests passed.\n");
    return 0;
}
