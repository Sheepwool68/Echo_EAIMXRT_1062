/*
 * bringup_config.h
 *
 * Staged hardware bring-up flags. Start with everything OFF except
 * APP_ENABLE_TCP, get that solid, then enable ONE more flag at a
 * time, rebuild, reflash, retest -- in roughly the order listed below.
 * Never enable a flag whose dependencies (noted per-flag) aren't
 * already working.
 *
 * Why compile-time #if rather than a runtime struct: disabled code is
 * fully stripped by the linker (a half-wired peripheral driver can't
 * accidentally get called), it's zero runtime cost, and there's
 * nothing to forget to initialize -- if the flag's off, app_init()
 * doesn't touch that hardware at all.
 */

#ifndef BRINGUP_CONFIG_H
#define BRINGUP_CONFIG_H

/* Stage 1 -- do this FIRST, before enabling any flag below:
 * flash a bare loop (BOARD_InitHardware() + a blinking LED, or a
 * "hello" print on your debug UART, no app_init() call at all) to
 * confirm clocks/pins are sane before any application logic runs.
 * Not a flag -- just don't call app_init()/app_run_one_iteration()
 * yet for this stage. */

/* Stage 2: TCP listener + PC command dispatch.
 * Depends on: Ethernet PHY/driver + lwIP bring-up (outside this port's
 * scope). Easiest to verify -- connect from a PC with telnet/netcat to
 * port 23 and check you get the "Connected,...,U\n" greeting. */
#define APP_ENABLE_TCP        1

/* Stage 2.5: nRF52833 SPI link (Active/LF reader link + chip programming).
 * Depends on: nothing else in this list, but the reader-power/channel
 * push at boot only makes sense once you've confirmed basic SPI
 * transfer works -- verify with a get-firmware-version round trip
 * first (nrf_spi_get_fw_version), printed over your debug UART,
 * before trusting the rest of this stage. */
#define APP_ENABLE_NRF_SPI    0

/* Stage 3: DS3231 RTC over I2C.
 * Depends on: nothing else in this list. Verify by reading the time
 * once at boot and printing it over your debug UART -- sane
 * year/month/day means I2C wiring + BCD conversion are both correct. */
#define APP_ENABLE_TIME_SYNC  0

/* Stage 3.5: MP2731 charger + MAX17303 fuel gauge over I2C (same bus
 * as DS3231, distinct chips). Depends on: nothing else in this list,
 * but verify DS3231 I2C wiring works first since it's the same
 * physical bus -- if that's not talking, this won't be either.
 * Verify by reading MAX17303_REG_REPSOC after boot and confirming a
 * sane 0-100% state-of-charge value over your debug UART. */
#define APP_ENABLE_BMS         0

/* Stage 4: littlefs / QSPI NOR log storage.
 * Depends on: nothing else in this list, but do it before UHF/rewind
 * since those write/read through it. Verify by mounting, appending a
 * test record, closing, reopening, and reading it back. */
#define APP_ENABLE_STORAGE    0

/* Stage 5: UHF reader (SIM7200) over UART.
 * Depends on: APP_ENABLE_STORAGE (chip reads get logged), ideally
 * APP_ENABLE_TIME_SYNC too (records need real timestamps to be
 * meaningful, though they'll still parse/log with a zeroed clock). */
#define APP_ENABLE_UHF         0

/* Stage 6: rewind-while-reading (binary search + resend).
 * Depends on: APP_ENABLE_STORAGE and APP_ENABLE_TCP both working --
 * this replays logged records back out over a TCP client. */
#define APP_ENABLE_REWIND      0

/* Stage 7: 4G modem (GPRS/LTE remote reporting).
 * Depends on: APP_ENABLE_STORAGE (batches records from the log).
 * Last, since it's the most "optional" path and the one most likely
 * to be gated by cellular coverage/SIM provisioning during bring-up
 * rather than your own code. */
#define APP_ENABLE_GPRS        0

/* Not ported yet -- leave OFF until GENIE2.LIB/NEOM8T.lib are pasted
 * and their real implementations replace display_stub.h/gps_stub.h. */
#define APP_ENABLE_DISPLAY     0
#define APP_ENABLE_GPS         0

#endif /* BRINGUP_CONFIG_H */
