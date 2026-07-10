#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "gprs_config_record.h"

static void test_struct_size(void) {
    assert(sizeof(gprs_remote_config_rec_t) == 101);
    printf("test_struct_size OK (size=%zu)\n", sizeof(gprs_remote_config_rec_t));
}

static gprs_config_inputs_t make_base_inputs(void) {
    gprs_config_inputs_t in;
    memset(&in, 0, sizeof(in));
    in.ants = 0x0B;
    in.reader_power = 80;
    in.beeper = 1;
    in.channel = 4;
    in.time_zone = 8;
    in.rtc_seconds = 1717000000;
    in.send_data_to_remote = 1;
    in.rabbit_ip[0] = 192; in.rabbit_ip[1] = 168; in.rabbit_ip[2] = 1; in.rabbit_ip[3] = 90;
    for (int i = 0; i < 6; i++) in.mac_address[i] = (uint8_t)(0x10 + i);
    for (int i = 0; i < 35; i++) in.gps_coords[i] = (uint8_t)('A' + (i % 26));
    in.battery_percent = 77;
    in.chip_reads = 12345;
    return in;
}

static void test_antenna_bits(void) {
    gprs_config_inputs_t in = make_base_inputs();
    gprs_remote_config_rec_t out;
    gprs_build_config_record(&in, &out);

    assert(out.ant11 == 1);
    assert(out.ant12 == 0);
    assert(out.ant13 == 1);
    assert(out.ant14 == 1);
    assert(out.ant21 == 0 && out.ant22 == 0 && out.ant23 == 0 && out.ant24 == 0);

    printf("test_antenna_bits OK\n");
}

static void test_channel_parity_vs_ultra_id(void) {
    gprs_config_inputs_t in = make_base_inputs();
    gprs_remote_config_rec_t out;

    in.channel = 4;
    gprs_build_config_record(&in, &out);
    assert(out.channel == 0);
    assert(out.ultra_id == 5);

    in.channel = 3;
    gprs_build_config_record(&in, &out);
    assert(out.channel == 1);
    assert(out.ultra_id == 4);

    printf("test_channel_parity_vs_ultra_id OK\n");
}

static void test_program_state_logic(void) {
    gprs_config_inputs_t in = make_base_inputs();
    gprs_remote_config_rec_t out;

    in.uhf_system_mode = 1; in.is_reading = 1;
    gprs_build_config_record(&in, &out);
    assert(out.program_state == 2);

    in.uhf_system_mode = 1; in.is_reading = 0;
    gprs_build_config_record(&in, &out);
    assert(out.program_state == 0);

    in.uhf_system_mode = 0; in.is_reading = 0;
    gprs_build_config_record(&in, &out);
    assert(out.program_state == 2);

    printf("test_program_state_logic OK\n");
}

static void test_gps_coords_and_hardcoded_fields(void) {
    gprs_config_inputs_t in = make_base_inputs();
    gprs_remote_config_rec_t out;
    gprs_build_config_record(&in, &out);

    for (int i = 0; i < 34; i++) {
        assert(out.gps_coords[i] == in.gps_coords[i]);
    }
    assert(out.gps_coords[34] == 0x02);

    assert(out.reader_mode[0] == 3 && out.reader_mode[1] == 3);
    assert(out.gating_interval == 3);
    assert(out.gating_mode == 1);
    assert(out.battery_type == 1);
    assert(out.no_chip_reads2 == 0);
    assert(out.reader_power[1] == 0);

    assert(out.end_chr[0] == 0x03 && out.end_chr[1] == 0x0D && out.end_chr[2] == 0x0A);
    assert(out.start_chr == 0x03);
    assert(out.rec_len == sizeof(out));

    printf("test_gps_coords_and_hardcoded_fields OK\n");
}

static void test_mac_and_ip_and_misc_passthrough(void) {
    gprs_config_inputs_t in = make_base_inputs();
    gprs_remote_config_rec_t out;
    gprs_build_config_record(&in, &out);

    assert(out.mac_address[0] == in.mac_address[3]);
    assert(out.mac_address[1] == in.mac_address[4]);
    assert(out.mac_address[2] == in.mac_address[5]);

    assert(out.rabbit_ip[0] == 192 && out.rabbit_ip[3] == 90);
    assert(out.time_zone == 8);
    assert(out.time == 1717000000);
    assert(out.battery_level == 77);
    assert(out.no_chip_reads1 == 12345);
    assert(out.beeper == 1);
    assert(out.send_data_to_remote == 1);
    assert(out.reader_power[0] == 80);

    printf("test_mac_and_ip_and_misc_passthrough OK\n");
}

int main(void) {
    test_struct_size();
    test_antenna_bits();
    test_channel_parity_vs_ultra_id();
    test_program_state_logic();
    test_gps_coords_and_hardcoded_fields();
    test_mac_and_ip_and_misc_passthrough();
    printf("\nAll gprs_config_record tests passed.\n");
    return 0;
}
