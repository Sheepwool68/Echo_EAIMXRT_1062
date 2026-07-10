#include "genie_protocol.h"
#include <string.h>

uint8_t genie_checksum(const uint8_t *data, size_t len)
{
    uint8_t checksum = 0;
    size_t i;
    for (i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

int genie_build_read_obj_frame(uint8_t object, uint8_t index, uint8_t *out)
{
    out[0] = (uint8_t)GENIE_READ_OBJ;
    out[1] = object;
    out[2] = index;
    out[3] = genie_checksum(out, 3);
    return 4;
}

int genie_build_write_obj_frame(uint8_t object, uint8_t index, uint16_t data, uint8_t *out)
{
    out[0] = (uint8_t)GENIE_WRITE_OBJ;
    out[1] = object;
    out[2] = index;
    out[3] = (uint8_t)((data >> 8) & 0xFFu);
    out[4] = (uint8_t)(data & 0xFFu);
    out[5] = genie_checksum(out, 5);
    return 6;
}

int genie_build_write_contrast_frame(uint8_t value, uint8_t *out)
{
    if (value) {
        value = (uint8_t)(value + 2);
    }
    out[0] = (uint8_t)GENIE_WRITE_CONTRAST;
    out[1] = value;
    out[2] = genie_checksum(out, 2);
    return 3;
}

int genie_build_write_str_frame(uint8_t index, const char *string, uint8_t *out, size_t out_size)
{
    size_t len = strlen(string);
    size_t i;

    if (len > 255) {
        return -1;
    }
    if (out_size < len + 3) {
        return -1;
    }

    out[0] = (uint8_t)GENIE_WRITE_STR;
    out[1] = index;
    out[2] = (uint8_t)len;
    for (i = 0; i < len; i++) {
        out[3 + i] = (uint8_t)string[i];
    }
    out[3 + len] = genie_checksum(out, 3 + len);
    return (int)(4 + len);
}

int genie_build_write_inh_label_frame(uint8_t index, const char *string, uint8_t *out, size_t out_size)
{
    size_t len = strlen(string);
    size_t i;

    if (len > 255) {
        return -1;
    }
    if (out_size < len + 3) {
        return -1;
    }

    out[0] = (uint8_t)GENIE_WRITE_INH_LABEL;
    out[1] = index;
    out[2] = (uint8_t)len;
    for (i = 0; i < len; i++) {
        out[3 + i] = (uint8_t)string[i];
    }
    out[3 + len] = genie_checksum(out, 3 + len);
    return (int)(4 + len);
}

int genie_build_magic_bytes_frame(uint8_t index, const uint8_t *bytes, uint8_t len, uint8_t *out)
{
    int i;
    out[0] = (uint8_t)GENIEM_WRITE_BYTES;
    out[1] = index;
    out[2] = len;
    for (i = 0; i < len; i++) {
        out[3 + i] = bytes[i];
    }
    out[3 + len] = genie_checksum(out, (size_t)(3 + len));
    return 4 + len;
}

int genie_build_magic_dbytes_frame(uint8_t index, const uint16_t *shorts, uint8_t len, uint8_t *out)
{
    int i;
    out[0] = (uint8_t)GENIEM_WRITE_DBYTES;
    out[1] = index;
    out[2] = len;
    for (i = 0; i < len; i++) {
        out[3 + 2 * i] = (uint8_t)((shorts[i] >> 8) & 0xFFu);
        out[3 + 2 * i + 1] = (uint8_t)(shorts[i] & 0xFFu);
    }
    out[3 + 2 * len] = genie_checksum(out, (size_t)(3 + 2 * len));
    return 4 + 2 * len;
}

int genie_parse_report_frame(const uint8_t bytes[GENIE_FRAME_SIZE], genie_frame_t *out_frame)
{
    uint8_t checksum_verify = genie_checksum(bytes, 5);

    out_frame->cmd = bytes[0];
    out_frame->object = bytes[1];
    out_frame->index = bytes[2];
    out_frame->data_msb = bytes[3];
    out_frame->data_lsb = bytes[4];

    return (checksum_verify == bytes[5]) ? 1 : 0;
}

void genie_event_queue_init(genie_event_queue_t *q)
{
    memset(q, 0, sizeof(*q));
}

int genie_event_queue_enqueue(genie_event_queue_t *q, const genie_frame_t *frame)
{
    int found = 0;
    int i, j;

    if (q->n_events >= MAX_GENIE_EVENTS - 2) {
        return 0;
    }

    j = q->wr_index;
    for (i = q->n_events; i > 0; i--) {
        j--;
        if (j < 0) {
            j = MAX_GENIE_EVENTS - 1;
        }
        if (q->frames[j].cmd == frame->cmd && q->frames[j].object == frame->object
            && q->frames[j].index == frame->index) {
            q->frames[j].data_msb = frame->data_msb;
            q->frames[j].data_lsb = frame->data_lsb;
            found = 1;
            break;
        }
    }

    if (!found) {
        q->frames[q->wr_index] = *frame;
        q->wr_index = (uint8_t)((q->wr_index + 1) & (MAX_GENIE_EVENTS - 1));
        q->n_events++;
    }

    return found;
}

int genie_event_queue_dequeue(genie_event_queue_t *q, genie_frame_t *out)
{
    if (q->n_events == 0) {
        return 0;
    }
    *out = q->frames[q->rd_index];
    q->rd_index = (uint8_t)((q->rd_index + 1) & (MAX_GENIE_EVENTS - 1));
    q->n_events--;
    return 1;
}

int genie_frame_is(const genie_frame_t *f, uint8_t cmd, uint8_t object, uint8_t index)
{
    return (f->cmd == cmd && f->object == object && f->index == index);
}

uint16_t genie_frame_get_data(const genie_frame_t *f)
{
    return (uint16_t)(((uint16_t)f->data_msb << 8) + f->data_lsb);
}
