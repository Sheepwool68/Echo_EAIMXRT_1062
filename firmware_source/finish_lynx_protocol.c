#include "finish_lynx_protocol.h"
#include "civil_time.h"
#include <stdio.h>
#include <string.h>

int finish_lynx_build_time_string(int hour, int min, int sec, int ms, char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "%cT,%02d:%02d:%02d.%03d\r\n",
                      0x01, hour, min, sec, ms);
    if (n < 0 || (size_t)n >= out_size) {
        return -1;
    }
    return n;
}

int finish_lynx_build_split_string(const nrf_record_t *rec, int is_rewind, char *out, size_t out_size)
{
    char chip_buf[7];
    int year, mon, mday, hour, minute, sec, wday;
    int n;

    (void)is_rewind; /* unused in the original too -- see header comment */

    /* Was `ulTime = logEntry.date_time; mktm(&CurTime, ulTime);` --
     * the original decodes date_time into hour/min/sec internally
     * (takes the whole record, not pre-decoded time parts). Using
     * civil_epoch_to_ymdhms() here since it's the equivalent
     * dependency-free decoder already established elsewhere in this
     * port (civil_time.h has no I/O of its own, so pulling it in here
     * doesn't compromise this file's "pure logic, no I/O" scope). */
    civil_epoch_to_ymdhms((int64_t)rec->date_time, &year, &mon, &mday, &hour, &minute, &sec, &wday);

    /* Was `sprintf(sChip, "%.6s", logEntry.xpdr_code)` -- xpdr_code is
     * NOT NUL-terminated (a raw 6-byte field), so "%.6s" stops at
     * either 6 bytes or an embedded NUL, whichever comes first.
     * Copying into a 7-byte local buffer with an explicit NUL placed
     * right after the 6 copied bytes reproduces that exact behavior
     * safely with plain "%s" below (stops at the first NUL either
     * way, embedded or the one just added). */
    memcpy(chip_buf, rec->xpdr_code, 6);
    chip_buf[6] = '\0';

    n = snprintf(out, out_size, "%cS,%02d:%02d:%02d.%03d,%s\r\n",
                 0x01, hour, minute, sec, rec->ms, chip_buf);
    if (n < 0 || (size_t)n >= out_size) {
        return -1;
    }
    return n;
}
