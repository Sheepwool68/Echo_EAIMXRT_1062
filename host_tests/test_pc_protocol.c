#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "pc_protocol.h"

static void test_classify_simple_commands(void) {
    pc_parsed_command_t r;

    r = pc_classify_command((const uint8_t *)"\x39", 1); /* '9' stop rewind */
    assert(r.id == PC_CMD_STOP_REWIND);

    r = pc_classify_command((const uint8_t *)"S", 1);
    assert(r.id == PC_CMD_UHF_STOP);

    r = pc_classify_command((const uint8_t *)"R", 1);
    assert(r.id == PC_CMD_UHF_START);

    r = pc_classify_command((const uint8_t *)"7XA0", 4);
    assert(r.id == PC_CMD_START_LIVE_DATA);

    r = pc_classify_command((const uint8_t *)"s", 1);
    assert(r.id == PC_CMD_STOP_LIVE_DATA);

    r = pc_classify_command((const uint8_t *)"r", 1);
    assert(r.id == PC_CMD_GET_TIME);

    r = pc_classify_command((const uint8_t *)"?", 1);
    assert(r.id == PC_CMD_GET_READING_STATUS);

    r = pc_classify_command((const uint8_t *)"U", 1);
    assert(r.id == PC_CMD_GET_SETTINGS);

    r = pc_classify_command((const uint8_t *)"u", 1);
    assert(r.id == PC_CMD_SET_SETTINGS);

    r = pc_classify_command((const uint8_t *)"B", 1);
    assert(r.id == PC_CMD_BATTERY_QUERY);

    {
        uint8_t buf[1] = { 0x03 };
        r = pc_classify_command(buf, 1);
        assert(r.id == PC_CMD_REMOTE_CONFIG);
    }

    r = pc_classify_command((const uint8_t *)"Z", 1); /* unknown */
    assert(r.id == PC_CMD_UNKNOWN);

    r = pc_classify_command((const uint8_t *)"", 0); /* empty buffer */
    assert(r.id == PC_CMD_UNKNOWN);

    printf("test_classify_simple_commands OK\n");
}

static void test_rewind_by_time_with_range(void) {
    /* '8' + split-char + antenna-char + "1000" + CR + "2000" */
    uint8_t buf[] = "8XA1000\r2000";
    pc_parsed_command_t r = pc_classify_command(buf, sizeof(buf) - 1);
    assert(r.id == PC_CMD_REWIND);
    assert(r.params.rewind.type == REWIND_BY_TIME);
    assert(r.params.rewind.from_time == 1000);
    assert(r.params.rewind.to_time == 2000);
    printf("test_rewind_by_time_with_range OK\n");
}

static void test_rewind_by_recno_no_upper_bound(void) {
    /* '6' + split + antenna + "500" + CR + nothing after */
    uint8_t buf[] = "6XA500\r";
    pc_parsed_command_t r = pc_classify_command(buf, sizeof(buf) - 1);
    assert(r.id == PC_CMD_REWIND);
    assert(r.params.rewind.type == REWIND_BY_RECNO);
    assert(r.params.rewind.from_time == 500);
    assert(r.params.rewind.to_time == 0);
    printf("test_rewind_by_recno_no_upper_bound OK\n");
}

static void test_rewind_malformed_no_cr(void) {
    /* No CR anywhere -- original would walk off the buffer; we must
     * detect this and report malformed instead. */
    uint8_t buf[] = "8XA1000NOCRHERE";
    pc_parsed_command_t r = pc_classify_command(buf, sizeof(buf) - 1);
    assert(r.id == PC_CMD_MALFORMED);
    printf("test_rewind_malformed_no_cr OK\n");
}

static void test_rewind_malformed_zero_from_all_zero(void) {
    /* Special case: FromTime == 0 is legal (means "full rewind" upstream) */
    uint8_t buf[] = "8XA0\r0";
    pc_parsed_command_t r = pc_classify_command(buf, sizeof(buf) - 1);
    assert(r.id == PC_CMD_REWIND);
    assert(r.params.rewind.from_time == 0);
    assert(r.params.rewind.to_time == 0);
    printf("test_rewind_malformed_zero_from_all_zero OK\n");
}

static void test_set_time_exact(void) {
    uint8_t buf[] = "t 11:50:27 08-10-2018";
    assert(strlen((char*)buf) == 21);
    pc_parsed_command_t r = pc_classify_command(buf, strlen((char*)buf));
    assert(r.id == PC_CMD_SET_TIME);
    assert(r.params.datetime.hour == 11);
    assert(r.params.datetime.min == 50);
    assert(r.params.datetime.sec == 27);
    assert(r.params.datetime.mday == 8);
    assert(r.params.datetime.mon == 10);
    assert(r.params.datetime.year == 2018);
    printf("test_set_time_exact OK\n");
}

static void test_set_time_with_stray_extra_byte(void) {
    /* Simulates the known RFIDServer quirk: one extra trailing byte.
     * pc_sanitize_command_length should truncate it away before parsing. */
    uint8_t buf[] = "t 11:50:27 08-10-2018X";
    pc_parsed_command_t r = pc_classify_command(buf, strlen((char*)buf));
    assert(r.id == PC_CMD_SET_TIME);
    assert(r.params.datetime.year == 2018);
    printf("test_set_time_with_stray_extra_byte OK\n");
}

static void test_set_time_too_short(void) {
    uint8_t buf[] = "t 11:50";
    pc_parsed_command_t r = pc_classify_command(buf, strlen((char*)buf));
    assert(r.id == PC_CMD_MALFORMED);
    printf("test_set_time_too_short OK\n");
}

static void test_set_output_type(void) {
    uint8_t buf_dec[2] = { 0x09, 0 };
    uint8_t buf_hex[2] = { 0x09, 1 };
    uint8_t buf_bad[2] = { 0x09, 5 };

    pc_parsed_command_t r = pc_classify_command(buf_dec, 2);
    assert(r.id == PC_CMD_SET_OUTPUT_TYPE);
    assert(r.params.output_type == 0);

    r = pc_classify_command(buf_hex, 2);
    assert(r.id == PC_CMD_SET_OUTPUT_TYPE);
    assert(r.params.output_type == 1);

    r = pc_classify_command(buf_bad, 2);
    assert(r.id == PC_CMD_MALFORMED);

    printf("test_set_output_type OK\n");
}

static void test_reading_status_bytes(void) {
    uint8_t out[5];
    pc_build_reading_status(1, 1, out);
    assert(memcmp(out, "\x53\x3D\x31\x31\x0A", 5) == 0);

    pc_build_reading_status(0, 0, out);
    assert(memcmp(out, "\x53\x3D\x30\x30\x0A", 5) == 0);

    printf("test_reading_status_bytes OK\n");
}

static void test_datetime_reply_format(void) {
    pc_datetime_fields_t t = { 9, 5, 3, 8, 10, 2018 };
    char buf[64];
    int n = pc_format_datetime_reply(&t, 123456789L, buf, sizeof(buf));
    assert(n > 0);
    assert(strcmp(buf, "09:05:03 08-10-2018 (123456789)\r\n") == 0);
    printf("test_datetime_reply_format OK -> [%s]\n", buf);
}

static void test_battery_status_format(void) {
    char buf[32];
    pc_format_battery_status(77, buf, sizeof(buf));
    assert(strcmp(buf, "V=77\n") == 0);
    printf("test_battery_status_format OK\n");
}

static void test_connect_greeting_format(void) {
    char buf[64];
    pc_build_connect_greeting(1717000000UL, buf, sizeof(buf));
    assert(strcmp(buf, "Connected,1717000000,U\n") == 0);
    printf("test_connect_greeting_format OK\n");
}

static void test_setting_formatters(void) {
    char buf[64];

    pc_format_setting_int(0x1F, 4, buf, sizeof(buf));
    assert(buf[0] == 'U' && buf[1] == (char)0x1F);
    assert(strcmp(buf + 2, "4\n") == 0);

    pc_format_setting_long(0x1E, 3UL, buf, sizeof(buf));
    assert(strcmp(buf + 2, "3\n") == 0);

    pc_format_setting_str(0x04, "myapn", buf, sizeof(buf));
    assert(strcmp(buf + 2, "myapn\n") == 0);

    printf("test_setting_formatters OK\n");
}

static void test_discovery_reply(void) {
    uint8_t mac[6] = { 0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33 };
    uint8_t out[64];
    size_t out_len = 0;

    int ok = pc_build_discovery_reply(mac, out, sizeof(out), &out_len);
    assert(ok);
    assert(out_len == 38);

    /* prefix */
    uint8_t expected_prefix[8] = {0x01, 0x09, 0x1E, 0x00, 0xDC, 0x05, 0x00, 0x00};
    assert(memcmp(out, expected_prefix, 8) == 0);

    /* name */
    assert(memcmp(&out[8], "Ultra (Echo)", 12) == 0);
    assert(out[20] == 0x00);

    /* mac string */
    assert(memcmp(&out[21], "aa:bb:cc:11:22:33", 17) == 0);

    /* Buffer-too-small must fail cleanly, not overflow */
    uint8_t small[10];
    size_t small_len = 0;
    ok = pc_build_discovery_reply(mac, small, sizeof(small), &small_len);
    assert(!ok);

    printf("test_discovery_reply OK\n");
}

static void test_start_live_data_with_from_time(void) {
    uint8_t buf[] = "7XA1717000000";
    pc_parsed_command_t r = pc_classify_command(buf, sizeof(buf) - 1);
    assert(r.id == PC_CMD_START_LIVE_DATA);
    assert(r.params.start_live_data.from_time == 1717000000);
    printf("test_start_live_data_with_from_time OK\n");
}

static void test_start_live_data_zero_from_time(void) {
    uint8_t buf[] = "7XA0";
    pc_parsed_command_t r = pc_classify_command(buf, sizeof(buf) - 1);
    assert(r.id == PC_CMD_START_LIVE_DATA);
    assert(r.params.start_live_data.from_time == 0);
    printf("test_start_live_data_zero_from_time OK\n");
}

static void test_start_live_data_no_digits(void) {
    uint8_t buf[] = "7XA";
    pc_parsed_command_t r = pc_classify_command(buf, sizeof(buf) - 1);
    assert(r.id == PC_CMD_START_LIVE_DATA);
    assert(r.params.start_live_data.from_time == 0);
    printf("test_start_live_data_no_digits OK\n");
}

int main(void) {
    test_classify_simple_commands();
    test_rewind_by_time_with_range();
    test_rewind_by_recno_no_upper_bound();
    test_rewind_malformed_no_cr();
    test_rewind_malformed_zero_from_all_zero();
    test_set_time_exact();
    test_set_time_with_stray_extra_byte();
    test_set_time_too_short();
    test_set_output_type();
    test_start_live_data_with_from_time();
    test_start_live_data_zero_from_time();
    test_start_live_data_no_digits();
    test_reading_status_bytes();
    test_datetime_reply_format();
    test_battery_status_format();
    test_connect_greeting_format();
    test_setting_formatters();
    test_discovery_reply();
    printf("\nAll pc_protocol tests passed.\n");
    return 0;
}
