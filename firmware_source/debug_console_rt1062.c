/*
 * debug_console_rt1062.c
 *
 * See debug_console_rt1062.h. vsnprintf() does pure in-memory formatting
 * (no syscall, no semihosting trap) -- only the final lpuart5_console_write()
 * touches hardware, and that's a real UART, not a debug-probe-dependent
 * mechanism.
 */

#include "debug_console_rt1062.h"
#include "lpuart5_console_rt1062.h"
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

#define DEBUG_PRINTF_BUF_SIZE 256u

int debug_printf(const char *fmt, ...)
{
    char buf[DEBUG_PRINTF_BUF_SIZE];
    va_list args;
    int n;

    va_start(args, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        size_t len = (size_t)n;
        if (len >= sizeof(buf)) {
            len = sizeof(buf) - 1; /* vsnprintf truncated -- only what's
                actually in buf was written, clamp to that */
        }
        lpuart5_console_write((const uint8_t *)buf, len);
    }
    return n;
}
