#include "ip_addr_parse.h"
#include <stdlib.h>

int ip_addr_parse_dotted_quad(const char *s, uint32_t *out_addr)
{
    int octets[4];
    int i;
    const char *p = s;

    if (s == NULL) {
        return 0;
    }

    for (i = 0; i < 4; i++) {
        char *end;
        long val;

        if (*p < '0' || *p > '9') {
            return 0;
        }

        val = strtol(p, &end, 10);
        if (val < 0 || val > 255) {
            return 0;
        }
        octets[i] = (int)val;
        p = end;

        if (i < 3) {
            if (*p != '.') {
                return 0;
            }
            p++;
        }
    }

    if (*p != '\0') {
        return 0;
    }

    *out_addr = ((uint32_t)octets[0] << 24)
              | ((uint32_t)octets[1] << 16)
              | ((uint32_t)octets[2] << 8)
              | (uint32_t)octets[3];
    return 1;
}
