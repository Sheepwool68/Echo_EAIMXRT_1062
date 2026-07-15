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
 * year/month/day means I2C wiring + BCD conversion are both correct.
 * CONFIRMED WORKING on real hardware 2026-07-14: process_time_sync()
 * (app_loop.c) depends on ds3231_rt1062_poll_rollover(), which relies
 * on the DS3231's 1Hz SQW output driving a GPIO1-pin-3 falling-edge
 * interrupt (TIMEPULSE_Handler) -- a path the earlier plain-I2C-read
 * DS3231 test never actually exercised. Added a dedicated check to the
 * real MCUXpresso project's hello_world.c bring-up test
 * (ds3231_rt1062_poll_rollover() logged once per fire) and confirmed
 * the ISR genuinely fires at the expected ~1Hz cadence before flipping
 * this flag -- see project memory. */
#define APP_ENABLE_TIME_SYNC  1

/* Stage 3.5: MP2731 charger + MAX17303 fuel gauge over I2C (same bus
 * as DS3231, distinct chips). Depends on: nothing else in this list,
 * but verify DS3231 I2C wiring works first since it's the same
 * physical bus -- if that's not talking, this won't be either.
 * Verify by reading MAX17303_REG_REPSOC after boot and confirming a
 * sane 0-100% state-of-charge value over your debug UART.
 * CONFIRMED WORKING on real hardware 2026-07-14: bms_init() (was
 * program_init()'s MP2731/MAX17303 setpoint section -- charge-current
 * limits, over/undervoltage protection thresholds, VEmpty, BATFET
 * config) writes real registers to the charger IC and fuel-gauge NV
 * memory, a different risk category than the read-only REPSOC check
 * above -- tested deliberately, not opportunistically, with readback
 * verification (not just bms_init()'s unconditional 0 return) added
 * to the real MCUXpresso project's hello_world.c bring-up test. User
 * confirmed the readback values matched what was written -- see
 * project memory. */
#define APP_ENABLE_BMS         1

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

/* Stage 8: Genie 4D display over LPUART2.
 * GENIE.LIB ported and display_stub.h backed by a real implementation
 * (genie_protocol.c/genie_display.c/genie_transport_rt1062.c) since an
 * earlier session; CONFIRMED WORKING on real hardware 2026-07-14 (splash
 * string round-tripped correctly via the separate real MCUXpresso
 * project's hello_world.c bring-up test -- see project memory). Depends
 * on: nothing else in this list. Touch/event input (display -> RT1062)
 * not yet separately confirmed, only text-out (RT1062 -> display) so
 * far -- worth watching for once this flag is live in the real app. */
#define APP_ENABLE_DISPLAY     1

/* Stage 9: NEO-M8T GPS over LPSPI3 (shares the bus with the nRF52833).
 * NEOM8T.LIB ported and gps_stub.h backed by a real implementation
 * (neo_m8t_protocol.c/neo_m8t_reader.c/neo_m8t_transport_rt1062.c)
 * since an earlier session; the underlying SPI transport is CONFIRMED
 * WORKING on real hardware (see project memory, project_gps_spi_bringup)
 * -- but that confirmation was via hello_world.c calling the transport
 * directly, not through this flag/gps_stub.h's higher-level
 * gps_configure_timepulse()/gps_sync_time_from_fix() path, so this
 * flag itself is still unexercised. Left OFF here rather than flipped
 * alongside APP_ENABLE_DISPLAY above -- enable deliberately, one flag
 * at a time per this file's own convention, when ready to retest. */
#define APP_ENABLE_GPS         0

#endif /* BRINGUP_CONFIG_H */
