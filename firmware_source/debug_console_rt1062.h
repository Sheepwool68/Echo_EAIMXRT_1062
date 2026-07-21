/*
 * debug_console_rt1062.h
 *
 * NEW, 2026-07-14 -- was PRINTF() aliasing to the toolchain's own printf()
 * (fsl_debug_console.h, SDK_DEBUGCONSOLE=0 -> DEBUGCONSOLE_REDIRECT_TO_TOOLCHAIN),
 * which the .cproject's linker library selection ("semihost_nf") routes over
 * ARM semihosting -- a special breakpoint instruction the attached debug
 * probe must service on every single call. If the SWD/LPC-Link connection
 * drops for ANY reason (probe hiccup, cable, or a real firmware bug), the
 * very next PRINTF() blocks forever waiting for a debugger that isn't
 * there -- indistinguishable, from the log alone, from the firmware itself
 * having hung at that exact line. This caused real confusion during this
 * port's bring-up (a genuine SWD lockout and a silent semihosting freeze
 * looked identical in the console log).
 *
 * debug_printf() below is a drop-in PRINTF replacement that formats with
 * vsnprintf() (pure in-memory formatting, no I/O syscall, no semihosting
 * involvement) and writes the result over LPUART5 (the FTDI USB-serial
 * console, already confirmed working real hardware -- see
 * lpuart5_console_rt1062.c). This decouples "is my firmware alive" from
 * "is the debug probe happy": if LPUART5 output stops, the firmware
 * genuinely hung; if it keeps going, you know the debug link -- not your
 * code -- is what dropped.
 */

#ifndef DEBUG_CONSOLE_RT1062_H
#define DEBUG_CONSOLE_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * printf-style output over LPUART5, independent of the SWD/semihosting
 * debug link. Call lpuart5_console_rt1062_init() before the first use
 * (must run after BOARD_InitBootPins()/BOARD_InitBootClocks() -- same
 * requirement lpuart5_console_rt1062_init() itself already has).
 * Truncates (like snprintf) rather than overflowing if a single
 * formatted message exceeds the internal buffer.
 */
int debug_printf(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_CONSOLE_RT1062_H */
