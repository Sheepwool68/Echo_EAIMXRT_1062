#include "gprs_config_record.h"
#include <string.h>

void gprs_build_config_record(const gprs_config_inputs_t *in, gprs_remote_config_rec_t *out)
{
    int i;

    memset(out, 0, sizeof(*out));

    out->start_chr = 0x03;
    out->rec_len = (uint8_t)sizeof(*out);

    out->ant11 = (int16_t)((in->ants & 8) >> 3);
    out->ant12 = (int16_t)((in->ants & 4) >> 2);
    out->ant13 = (int16_t)((in->ants & 2) >> 1);
    out->ant14 = (int16_t)(in->ants & 1);
    out->ant21 = 0; out->ant22 = 0; out->ant23 = 0; out->ant24 = 0;

    out->beeper = in->beeper;

    out->reader_power[0] = in->reader_power;
    out->reader_power[1] = 0;

    out->reader_mode[0] = 3; /* "finish" mode, hardcoded */
    out->reader_mode[1] = 3;

    out->gating_interval = 3; /* hardcoded */
    out->ant4_is_backup[0] = 0;
    out->ant4_is_backup[1] = 0;
    out->gating_mode = 1; /* "per box", hardcoded */

    out->channel = 0;
    if ((in->channel + 1) % 2 == 0) {
        out->channel = 1; /* parity bit, NOT the channel number -- see ultra_id */
    }

    out->time_zone = in->time_zone;
    out->ultra_id = (uint16_t)(in->channel + 1);
    out->time = in->rtc_seconds;
    out->send_data_to_remote = in->send_data_to_remote;

    out->rabbit_ip[0] = in->rabbit_ip[0];
    out->rabbit_ip[1] = in->rabbit_ip[1];
    out->rabbit_ip[2] = in->rabbit_ip[2];
    out->rabbit_ip[3] = in->rabbit_ip[3];

    out->mac_address[0] = in->mac_address[3];
    out->mac_address[1] = in->mac_address[4];
    out->mac_address[2] = in->mac_address[5];

    for (i = 0; i < 34; i++) {
        out->gps_coords[i] = in->gps_coords[i];
    }
    out->gps_coords[34] = 0x02; /* device-type marker, overwrites whatever was in in->gps_coords[34] */

    out->battery_level = in->battery_percent;
    out->battery_type = 1; /* "percentage, not volts", hardcoded */

    /* Net effect of the original's two ProgramState assignments -- see
     * header note on the dead first assignment. */
    if (in->uhf_system_mode) {
        out->program_state = in->is_reading ? 2 : 0;
    } else {
        out->program_state = 2;
    }

    out->no_chip_reads1 = in->chip_reads;
    out->no_chip_reads2 = 0; /* single reader only */

    out->end_chr[0] = 0x03;
    out->end_chr[1] = 0x0D;
    out->end_chr[2] = 0x0A;
}
