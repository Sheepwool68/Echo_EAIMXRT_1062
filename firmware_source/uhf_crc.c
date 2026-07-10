#include "uhf_crc.h"

static const uint16_t kCrcTable[16] = {
    0x0000, 0x1021, 0x2042, 0x3063,
    0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b,
    0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
};

uint16_t uhf_calc_crc(const uint8_t *buf, size_t len)
{
    uint16_t crc = 0xFFFF;
    size_t i;

    for (i = 1; i < len; i++) {
        crc = (uint16_t)((crc << 4) | (buf[i] >> 4))   ^ kCrcTable[crc >> 12];
        crc = (uint16_t)((crc << 4) | (buf[i] & 0x0F)) ^ kCrcTable[crc >> 12];
    }

    return crc;
}

void uhf_add_crc(uint8_t *buf, size_t len)
{
    uint16_t crc = uhf_calc_crc(buf, len);
    buf[len] = (uint8_t)(crc >> 8);
    buf[len + 1] = (uint8_t)(crc & 0xFF);
}

int uhf_verify_crc(const uint8_t *buf, uint8_t frame_data_length)
{
    uint16_t computed = uhf_calc_crc(buf, (size_t)frame_data_length + 5);
    uint16_t received = (uint16_t)((buf[frame_data_length + 5] << 8)
                                    | buf[frame_data_length + 6]);
    return (computed == received) ? 1 : 0;
}
