#include "crc32.h"

static uint32_t crc32_reflect_byte(uint32_t crc, uint8_t byte)
{
    int i;
    crc ^= byte;
    for (i = 0; i < 8; i++) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0xEDB88320u;
        } else {
            crc >>= 1;
        }
    }
    return crc;
}

uint32_t crc32_update(uint32_t running_crc, const uint8_t *data, size_t len)
{
    size_t i;
    uint32_t crc = running_crc;
    for (i = 0; i < len; i++) {
        crc = crc32_reflect_byte(crc, data[i]);
    }
    return crc;
}

uint32_t crc32_finalize(uint32_t running_crc)
{
    return running_crc ^ 0xFFFFFFFFu;
}

uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    return crc32_finalize(crc32_update(CRC32_INITIAL, data, len));
}
