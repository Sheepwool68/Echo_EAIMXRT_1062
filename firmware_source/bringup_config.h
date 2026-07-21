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
 *
 * CURRENT STATE, 2026-07-16: the Genie-only pass (APP_ENABLE_DISPLAY
 * alone) was confirmed working end-to-end on real hardware 2026-07-15 --
 * app_genie_dispatch.c's touch-event routing is proven live through the
 * real app_init()/app_run_one_iteration() call sequence, not just a
 * standalone hello_world.c test. Per this project's staged-bring-up
 * discipline (one flag at a time, not all at once), this session added
 * APP_ENABLE_TCP first (confirmed booting; ENET link-up + DHCP-lease
 * diagnostics added to app_loop.c after "no lease" turned out ambiguous
 * -- see project memory), then APP_ENABLE_BMS (explicit user consent
 * given, since it writes real charger/fuel-gauge safety setpoints --
 * immediate motivation was app->board_version staying 0 with it off,
 * which silently zeroed the TCP "V=%d\n" battery-status line and the
 * Genie battery gauge alike), then APP_ENABLE_TIME_SYNC (read-only I2C,
 * same bus already confirmed live via BMS, previously confirmed
 * standalone via the TIMEPULSE ISR -- CONFIRMED WORKING on real
 * hardware same day, Genie MAIN screen's time visibly incrementing),
 * then APP_ENABLE_GPS (per explicit request for GPS-driven time sync
 * with timezone applied, showing correct date/time on the LCD --
 * process_time_sync() already had this wired from an earlier session,
 * just needed the flag on). APP_ENABLE_STORAGE was then briefly
 * enabled (per explicit request to make auto_set_gps_time a real
 * persistent setting) but REVERTED BACK OFF the same session after it
 * correlated with app_init() hanging completely inside
 * enet_lwip_rt1062_init(). That hang's REAL root cause was later found
 * and fixed (2026-07-16, same overall investigation, see project memory
 * project_enet_lwip_bringup.md): debug-probe/JTAG-shared-pin
 * interference during the PHY reset sequence, NOT Storage -- confirmed
 * both by testing with the probe physically disconnected (boot
 * completed fully) and by re-testing Storage's own ordering fix in
 * isolation. enet_lwip_rt1062_init() also gained a bounded PHY-preflight
 * retry + non-fatal failure path the same day, so even a residual PHY
 * glitch (e.g. on a probe-attached reflash) no longer takes the whole
 * boot down -- both fixes CONFIRMED WORKING together on real hardware
 * (full boot to MAIN, probe attached). APP_ENABLE_STORAGE re-enabled
 * 2026-07-16 per explicit request now that it's confirmed innocent.
 * Confirmed-good state: Display+TCP+BMS+TIME_SYNC+GPS+STORAGE.
 * Remaining flags (NRF_SPI, UHF, REWIND, GPRS, BOARD_IO) stay 0 as a
 * staging choice -- flip them back on one at a time, same discipline as
 * always. */

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
 * port 23 and check you get the "Connected,...,U\n" greeting.
 * CONFIRMED WORKING on real hardware in an earlier standalone session
 * (see project memory, project_enet_lwip_bringup) -- KSZ8081 RMII PHY +
 * lwIP round-tripped correctly once the FSL_FEATURE_PHYKSZ8081_USE_RMII50M_MODE
 * define was found and added.
 * ON 2026-07-16 -- second flag enabled alongside APP_ENABLE_DISPLAY, per
 * this file header's "CURRENT STATE" note. Verify the "Connected,...,U\n"
 * greeting over telnet/netcat to port 23 before moving to the next flag. */
#define APP_ENABLE_TCP        1

/* Stage 2.5: nRF52833 SPI link (Active/LF reader link + chip programming).
 * Depends on: nothing else in this list, but the reader-power/channel
 * push at boot only makes sense once you've confirmed basic SPI
 * transfer works -- verify with a get-firmware-version round trip
 * first (nrf_spi_get_fw_version), printed over your debug UART,
 * before trusting the rest of this stage.
 * CONFIRMED WORKING on real hardware 2026-07-14: app_init.c's SPI
 * transport init + GPS bus-priming step (confirmed genuinely required,
 * see neo_m8t_transport_rt1062_init() usage there) round-trips real,
 * non-sentinel replies over the link consistently. The exact
 * fw_version byte itself varies run-to-run (7, 32, etc. seen) --
 * resolved as an inherent test-methodology limitation, not an
 * RT1062-side bug: the nRF's 0x0E command is only meaningful the
 * FIRST time it's queried after a genuine nRF boot (confirmed by the
 * firmware's author), and every test so far reset only the RT1062
 * while the nRF stayed continuously powered across runs -- see
 * project memory. Getting any plausible non-sentinel byte back (not
 * the SPIS idle-default 0xBB/0xDD) is what actually confirms the link
 * works for this stage's purposes; a true cold power-cycle would be
 * needed to trust the specific version number.
 * ON 2026-07-17 -- per explicit instruction, next flag per the staged
 * order. Already fully wired through the current app_init()/app_loop()/
 * app_pc_dispatch.c/app_genie_dispatch.c flow (transport init + GPS
 * bus-priming step in app_init.c, process_nrf_spi() in app_loop.c,
 * reader-power/channel/BT-adv/playback/sleep/DFU commands in the PC and
 * Genie dispatchers) -- unlike APP_ENABLE_UHF, this stage needed no
 * missing-init-call fix, just the flag flip. Verify the same way as the
 * 2026-07-14 standalone confirmation: a plausible non-sentinel
 * fw_version byte (not 0xBB/0xDD) over the debug UART at boot. */
#define APP_ENABLE_NRF_SPI    1

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
 * this flag -- see project memory.
 * ON 2026-07-16 -- next flag per the staged order, read-only I2C on
 * the same bus already confirmed live via APP_ENABLE_BMS. Verify by
 * checking app->current_time (and records/status broadcasts that use
 * it) show a real date/time instead of the epoch-zero default. */
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
 * project memory.
 * ON 2026-07-16, explicit user consent given -- writes real safety-
 * relevant setpoints, same as 2026-07-14's confirmation, so re-asked
 * before flipping rather than doing it opportunistically alongside
 * Genie+TCP. Immediate motivation: app->board_version (only ever set
 * by this stage's own 0x4067 board-revision read) was staying at its
 * 0 default with this flag off, which silently suppressed BOTH the
 * Genie screen's battery gauge AND the new periodic MAX17303_REG_REPSOC
 * read added to app_loop.c's process_periodic_checks() the same day
 * (a stale "MP2731/charger library not provided" TODO -- the driver
 * has existed for a while, nobody had wired the periodic read in). */
#define APP_ENABLE_BMS         1

/* Stage 4: littlefs / QSPI NOR log storage.
 * Depends on: nothing else in this list, but do it before UHF/rewind
 * since those write/read through it. Verify by mounting, appending a
 * test record, closing, reopening, and reading it back.
 * CONFIRMED WORKING on real hardware 2026-07-14: uses lfs_mflash.c/
 * mflash_drv.c (NXP's own driver, chosen over the scaffold's earlier
 * untested nand_log_flash_qspi.c -- see that file's superseded header
 * comment and project memory for the full reasoning). Tested through
 * the actual app_init.c-facing layer, not just raw littlefs calls:
 * nand_log_mount()/nand_log_open_for_append()/nand_log_append_records()
 * (LFS_O_APPEND, batch nrf_record_t write + lfs_file_sync())/
 * nand_log_get_last_log_id()/nand_log_read_next_record() all round-
 * tripped correctly on the real MCUXpresso project's hello_world.c
 * bring-up test -- 2 dummy records appended, both log_id and full
 * content confirmed on readback.
 * ON 2026-07-16, per explicit request to make "set time by GPS"
 * (auto_set_gps_time) an actually-persistent setting, not something
 * re-toggled on the touchscreen after every reflash. This is real
 * settings persistence for EVERY field in device_settings_t, not a
 * special case for just this one -- settings_store.h already existed
 * and was unit-tested, just never exercised on hardware through the
 * real app_init()/app_loop() flow. Timely: the app_init.c ordering bug
 * where storage mount/settings-load ran too LATE (after several
 * hardware pushes had already used defaults) was found and fixed
 * earlier this same session, so this flag is safe to enable now in a
 * way it wasn't a few edits ago.
 * REVERTED BACK to 0, 2026-07-16, per explicit instruction ("Nothing.
 * Not even Lpuart5 responses. You need to go back to what worked.")
 * after enabling this flag correlated with app_init() hanging
 * completely inside enet_lwip_rt1062_init() (traced via boot-progress
 * checkpoints to somewhere inside netif_add()/PHY_Init()/MDIO wait --
 * see project memory, project_gps_spi_bringup.md, for the full
 * investigation including a real missing-compiler-define fix
 * (ENET_MDIO_TIMEOUT_COUNT) that did NOT resolve it). This was a
 * correlation, not a confirmed cause -- Storage happened to be the
 * most recent change before the hang first appeared.
 * ROOT CAUSE FOUND, same session, later: debug-probe/JTAG-shared-pin
 * (GPIO1 9/10) interference during enet_phy_hw_reset(), confirmed by
 * testing with the probe physically disconnected (boot completed
 * fully) -- NOT Storage. enet_lwip_rt1062_init() also gained a bounded
 * PHY-preflight retry + non-fatal failure path so a residual glitch
 * (e.g. on a probe-attached reflash) degrades to "no network this
 * boot" instead of hanging the whole board. See project memory,
 * project_enet_lwip_bringup.md, for the full second investigation.
 * RE-ENABLED 2026-07-16, per explicit request, now that it's confirmed
 * innocent and the app_init.c storage-load-ordering fix (mount/load
 * running first, before anything else touches app->settings) is
 * already in place from the earlier boot-order audit. */
#define APP_ENABLE_STORAGE    1

/* Stage 5: UHF reader (SIM7200) over UART.
 * Depends on: APP_ENABLE_STORAGE (chip reads get logged), ideally
 * APP_ENABLE_TIME_SYNC too (records need real timestamps to be
 * meaningful, though they'll still parse/log with a zeroed clock).
 * Both already on. Flipped 2026-07-17, next staged subsystem per
 * explicit instruction from the end of the 2026-07-16 session. Found
 * and fixed a real gap first: app->uhf_transport was never populated
 * anywhere (see app_init.c) -- would have NULL-pointer-called through
 * an unset transport the moment reading started. Individually
 * confirmed working (antenna detection, tag-read time-sync gate,
 * uhf_reader_start() call site) in an earlier standalone session, but
 * never yet run through this integrated app_init()/app_loop() flow. */
#define APP_ENABLE_UHF         1

/* Stage 6: rewind-while-reading (binary search + resend).
 * Depends on: APP_ENABLE_STORAGE and APP_ENABLE_TCP both working --
 * this replays logged records back out over a TCP client.
 * ON 2026-07-20 -- process_rewind() rewritten as a resumable state
 * machine with its own dedicated file handle (separate from the live-
 * append path) before enabling, so a rewind can run concurrently with
 * live chip reads without blocking the main loop -- see
 * nand_log_littlefs.h's rewind_file/rewind_open field comment and
 * project memory for the full rewrite. */
#define APP_ENABLE_REWIND      1

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
 * far -- worth watching for once this flag is live in the real app.
 * ON 2026-07-15, deliberately the ONLY flag on -- see file header's
 * "CURRENT STATE" note. This is the first real test of touch/event
 * input specifically: app_genie_dispatch.c's full touch-event router
 * (myGenieEventHandler port) is now live for the first time, with
 * app_loop.c's process_display_events() tracing every dequeued
 * object/index/data over the debug UART so touch routing can be
 * confirmed widget-by-widget before bringing anything else back up. */
#define APP_ENABLE_DISPLAY     1

/* Stage 9: NEO-M8T GPS over LPSPI3 (shares the bus with the nRF52833).
 * NEOM8T.LIB ported and gps_stub.h backed by a real implementation
 * (neo_m8t_protocol.c/neo_m8t_reader.c/neo_m8t_transport_rt1062.c)
 * since an earlier session; CONFIRMED WORKING END-TO-END on real
 * hardware 2026-07-14, through the actual higher-level path this flag
 * gates (neo_m8t_configure_timepulse()'s 6 UBX CFG writes -- byte-
 * verified against the real NEOM8T.LIB, not just the SPI transport --
 * PPS ISR firing, and gps_sync_time_from_fix_impl() succeeding with a
 * real satellite fix: "GPS time sync OK: 2026-07-15 04:43:46"). See
 * project memory (project_gps_spi_bringup) for the full investigation,
 * including a faithfully-preserved operator-precedence quirk from the
 * original C source in the fix-validity check.
 * ON 2026-07-16, per explicit request to get GPS-driven time sync
 * (with timezone applied -- process_time_sync() already passes
 * app->settings.time_zone/add30 into gps_sync_time_from_fix(), no new
 * code needed) showing correct date/time on the Genie LCD. No
 * dependency conflict with APP_ENABLE_NRF_SPI being off -- the shared-
 * bus GPS-priming concern only matters when nRF SPI queries also run
 * in the same boot, and it doesn't here. NOTE: auto_set_gps_time
 * (the touchscreen toggle actually gating whether a sync fires --
 * pps_should_trigger_gps_sync()) defaults OFF, faithful to the
 * original's own `Settings.AutoSetGPSTime = 0` boot default -- with
 * APP_ENABLE_STORAGE also off, this doesn't persist across reboots, so
 * it needs a touchscreen tap each boot until storage is staged. */
#define APP_ENABLE_GPS         1

/* Board GPIO-only inits (buzzer, reader shutdown/power, cooling fan) --
 * NOT part of the original staged list above; these were called
 * unconditionally from app_init() regardless of any flag, which meant
 * there was no way to leave them off for a narrow, single-subsystem
 * bring-up pass like this one. None are needed for the Genie-only pass
 * (all board GPIO, unrelated to LPUART2) -- gated behind their own flag
 * per explicit instruction, rather than commenting the call sites out,
 * so this stays consistent with the compile-time-#if convention used
 * everywhere else in this file. Safe to turn on any time -- these are
 * plain GPIO pin inits, no shared-bus ordering concerns like LPI2C1.
 * FLIPPED ON 2026-07-16, per explicit request, as the next staged
 * subsystem after the full Display+TCP+BMS+TIME_SYNC+GPS+STORAGE combo
 * was confirmed working together. buzzer_rt1062_init()/
 * reader_shutdown_rt1062_init()/fan_rt1062_init() were each previously
 * confirmed working individually in earlier standalone bring-up
 * sessions, but never yet through this real app_init()/app_loop() flow
 * alongside everything else currently staged. */
#define APP_ENABLE_BOARD_IO    1

#endif /* BRINGUP_CONFIG_H */
