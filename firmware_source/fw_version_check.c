#include "fw_version_check.h"
#include <stdlib.h>
#include <string.h>

int fw_parse_version_html(const char *body, size_t body_len,
                           size_t max_scan_chars, uint16_t *out_version)
{
    size_t n = 0;
    size_t i = 0;

    if (body == NULL || body_len < 4) {
        return 0;
    }

    while (n <= max_scan_chars && i + 4 <= body_len) {
        if (memcmp(&body[i], "<h1>", 4) == 0) {
            char fw_str[7];
            size_t remaining = body_len - (i + 4);
            size_t copy_len = (remaining < 6) ? remaining : 6;

            if (copy_len < 6) {
                return 0; /* not enough bytes left for a full 6-char version field */
            }
            memcpy(fw_str, &body[i + 4], 6);
            fw_str[6] = '\0';

            *out_version = (uint16_t)strtol(fw_str, NULL, 0);
            return 1;
        }
        i++;
        n++;
    }
    return 0;
}

int fw_parse_version_plain(const char *body, size_t body_len, uint16_t *out_version)
{
    char buf[16];
    size_t n = (body_len < sizeof(buf) - 1) ? body_len : sizeof(buf) - 1;

    if (body == NULL || body_len == 0) {
        return 0;
    }
    memcpy(buf, body, n);
    buf[n] = '\0';

    *out_version = (uint16_t)strtol(buf, NULL, 0);
    return 1;
}

fw_check_result_t fw_compare_versions(uint16_t current_version, uint16_t remote_version)
{
    return (current_version == remote_version) ? FW_CHECK_UP_TO_DATE : FW_CHECK_UPDATE_AVAILABLE;
}
