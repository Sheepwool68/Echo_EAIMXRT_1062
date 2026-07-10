#include "uhf_packet_parser.h"
#include "uhf_crc.h"

int uhf_parse_tag_read(const uint8_t *buf, size_t len, uhf_tag_read_t *out)
{
    uint8_t datalength;
    uint8_t reads, ant_no, pcword0;
    uint32_t pcwordbytes, chip_offset;
    int rssi;

    if (len < 23) {
        return 0; /* not enough bytes to even read the PC word field at buf[22] */
    }
    datalength = buf[1];
    if (len < (size_t)datalength + 7) {
        return 0; /* not enough bytes for the full frame + CRC */
    }
    if (!uhf_verify_crc(buf, datalength)) {
        return 0;
    }

    reads = buf[7];
    ant_no = buf[9];
    rssi = (int)buf[8] - 256;
    pcword0 = buf[22];
    pcwordbytes = (uint32_t)((pcword0 >> 3) * 2);
    chip_offset = 20u + pcwordbytes;

    if (len < (size_t)chip_offset + 4) {
        return 0; /* defensive bounds check -- not present in the original,
                     which would have read out of bounds on a short/corrupt frame */
    }

    out->chip_code = ((uint32_t)buf[chip_offset] << 24)
                    | ((uint32_t)buf[chip_offset + 1] << 16)
                    | ((uint32_t)buf[chip_offset + 2] << 8)
                    | (uint32_t)buf[chip_offset + 3];
    out->rssi = rssi;
    out->rssi_percent = (int)((double)rssi * 1.7 + 160.0);
    if (out->rssi_percent > 100) {
        out->rssi_percent = 100;
    }
    out->antenna = ant_no;
    out->reads = reads;
    return 1;
}

uint8_t uhf_parse_ant_status(const uint8_t *buf, size_t len)
{
    uint8_t ants = 0;
    size_t b = 5;
    int i;

    for (i = 0; i < 4; i++) {
        ants = (uint8_t)(ants << 1);
        b += 2;
        if (b >= len) {
            break; /* defensive -- not present in the original */
        }
        ants = (uint8_t)(ants | buf[b]);
    }
    return ants;
}

int uhf_parse_return_loss(const uint8_t *buf, size_t len, uhf_return_loss_t *out)
{
    int percent;
    uint8_t raw;
    uint8_t ant_no;

    if (len < 26) {
        return 0;
    }

    ant_no = buf[19];
    raw = buf[25];

    percent = (int)((double)raw / 2.5);
    if (percent > 100) {
        percent = 100;
    }
    if (raw < 100) {
        percent = 0; /* matches the original's explicit override */
    }

    out->antenna = ant_no;
    out->raw_value = raw;
    out->percent = percent;
    out->good = (raw < 100) ? 0 : 1;
    /* k is fixed at 8 in the original (no loop over antennas within this
     * function -- it's called once per antenna's test reply), so this
     * is a direct port of `ants | (k >> (ant_no - 1))`. */
    out->ant_bit = (ant_no >= 1 && ant_no <= 4) ? (uint8_t)(8 >> (ant_no - 1)) : 0;

    return 1;
}

size_t uhf_process_buffer(const uint8_t *buf, size_t len,
                           uhf_frame_event_cb cb, void *cb_ctx)
{
    size_t i = 0;

    /* Resync to the first sync byte. FIXED from the original's
     * `p[0]!=0xFF && p[2]!=0xAA` (an && where a single p[0] check is
     * the defensible reading of intent) -- see porting notes. */
    while (i < len && buf[i] != 0xFF) {
        i++;
    }

    while (i < len) {
        if (i + 6 > len) {
            break; /* not enough bytes left for even the smallest frame header */
        }

        if (buf[i + 2] == 0x61 && buf[i + 5] == 0x05) {
            /* Antenna status frame -- fixed 14-byte length per the
             * documented example ("FF 09 61 00 00 05 01 00 02 00 03 00 04 01"). */
            const size_t frame_len = 14;
            if (i + frame_len > len) {
                break; /* incomplete -- wait for more data */
            }
            {
                uhf_frame_event_t ev;
                ev.type = UHF_FRAME_ANT_STATUS;
                ev.data.ant_status_mask = uhf_parse_ant_status(&buf[i], frame_len);
                if (cb != NULL) {
                    cb(cb_ctx, &ev);
                }
            }
            i += frame_len;
        }
        else if (buf[i] == 0xFF && buf[i + 2] == 0xAA) {
            uint8_t b1 = buf[i + 1];
            size_t frame_len = (size_t)b1 + 7;

            if (i + frame_len > len) {
                break; /* incomplete AA-family frame -- wait for more data */
            }

            if (b1 == 0x06) {
                uhf_frame_event_t ev;
                ev.type = UHF_FRAME_HEARTBEAT;
                if (cb != NULL) cb(cb_ctx, &ev);
            }
            else if (b1 == 0x16 && (i + 21) < len && buf[i + 21] == 0x05) {
                uhf_frame_event_t ev;
                ev.type = UHF_FRAME_END_OF_ROUND;
                if (cb != NULL) cb(cb_ctx, &ev);
            }
            else if (b1 == 0x0c) {
                uhf_frame_event_t ev;
                ev.type = UHF_FRAME_START_CONFIRM;
                if (cb != NULL) cb(cb_ctx, &ev);
            }
            else if (b1 == 0x15) {
                uhf_frame_event_t ev;
                ev.type = UHF_FRAME_RETURN_LOSS;
                if (uhf_parse_return_loss(&buf[i], frame_len, &ev.data.return_loss)) {
                    if (cb != NULL) cb(cb_ctx, &ev);
                }
            }
            else {
                /* Tag-read candidate. Original: only attempts this if at
                 * least 40 bytes remain in the buffer from position i;
                 * otherwise it bails out of the WHOLE scan (not just this
                 * frame) with a "Message cut" comment, on the assumption
                 * a tag frame needs at least that many bytes to be
                 * complete. Preserved as a full-scan stop, matching the
                 * original's `return;` in that branch. */
                if ((len - i) < 40) {
                    return i; /* stop scanning; caller may retry with more data */
                }
                {
                    uhf_frame_event_t ev;
                    ev.type = UHF_FRAME_TAG_READ;
                    if (uhf_parse_tag_read(&buf[i], len - i, &ev.data.tag)) {
                        if (cb != NULL) cb(cb_ctx, &ev);
                    }
                }
                /* Note: frame_len here is (b1+7) as computed above, same
                 * as the original's iDataLen, regardless of whether the
                 * CRC/parse succeeded -- a corrupt frame still advances
                 * past its claimed length rather than getting stuck. */
            }

            i += frame_len;
        }
        else if (buf[i] == 0xFF && buf[i + 2] == 0x72) {
            /* Temperature report -- fixed 8-byte frame */
            const size_t frame_len = 8;
            if (i + frame_len > len) {
                break;
            }
            {
                uhf_frame_event_t ev;
                ev.type = UHF_FRAME_TEMPERATURE;
                ev.data.temperature = buf[i + 5];
                if (cb != NULL) {
                    cb(cb_ctx, &ev);
                }
            }
            i += frame_len;
        }
        else {
            /* Unrecognized -- resync by one byte, matching the original's
             * catch-all `p+=1; i+=1;`. */
            i += 1;
        }
    }

    return i;
}
