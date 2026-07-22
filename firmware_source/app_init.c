/*
 * app_init.c
 *
 * ==========================================================================
 * INTEGRATION SCAFFOLD -- like the hardware transport files, this cannot be
 * compiled in this environment (it transitively pulls in every hardware
 * header via app_context.h). Unlike the individual hardware scaffolds
 * though, this file's correctness rests entirely on all the OTHER modules
 * being wired correctly -- it's the glue, not an algorithm, so there's
 * nothing here to unit test the way there was for the protocol modules.
 * Review carefully against the original's setup sequence before trusting
 * it; I'd treat this as a first draft to integration-test against real
 * hardware, not a finished, verified port.
 * ==========================================================================
 *
 * Was the setup portion of main() before the for(;;) loop.
 *
 * GATED BY bringup_config.h: each hardware subsystem's init only runs if
 * its flag is on, so you can bring the board up one piece at a time. See
 * bringup_config.h for the recommended order and per-flag dependencies.
 */

#include "app_context.h"
#include "app_pc_dispatch.h"
#include "app_genie_dispatch.h"
#include "display_stub.h"
#include "gps_stub.h"
#include "civil_time.h"
#include "bringup_config.h"
#include "systick_ms_rt1062.h"
#include "nrf_spi_protocol.h"
#include "nrf_spi_transport_rt1062.h"
#include "neo_m8t_transport_rt1062.h"
#include "uhf_transport_rt1062.h"
#include "settings_store.h"
#include "bms_init.h"
#include "buzzer_rt1062.h"
#include "reader_shutdown_rt1062.h"
#include "fan_rt1062.h"
#include "button_led_rt1062.h"
#include "lpi2c1_bus_rt1062.h"
#include "finish_lynx_protocol.h"
#include "enet_lwip_rt1062.h"
#include "lfs_mflash.h"
#include "debug_console_rt1062.h"
#include "fsl_common.h"
#include <string.h>

/* PRINTF redirect to LPUART5 -- see debug_console_rt1062.h. Needed here
 * for the display-detection diagnostic below (added 2026-07-15, first
 * real hardware pass -- app_init() previously reported nothing about
 * whether display_init() actually found the display, matching the
 * original's own lack of a hard failure path there, but leaving this
 * genuinely useful diagnostic bit undiscoverable when debugging a
 * silent screen). */
#undef PRINTF
#define PRINTF debug_printf

int app_init(app_context_t *app)
{
    memset(app, 0, sizeof(*app));

    /* -1 = "no WinButton has opened a KEYBOARD/KEYPAD form yet" -- 0
     * would collide with the real GENIE_BUTTON_SETTINGS enum value, see
     * app_context.h's field comment. Every other touchscreen UI field
     * is fine left at memset's 0. */
    app->last_winbutton = -1;

#if APP_ENABLE_BOARD_IO
    /* CONFIRMED from the original, ACTIVERFID_V1.02_UHF.c lines 3454-3457,
     * main(): `BitWrPortI(PBDDR,...,1,2); BitWrPortI(PBDR,...,1,2); //Buzzer On`
     * runs BEFORE `msDelay(2000)` -- the buzzer sounds continuously for
     * the entire startup delay (and the rest of program_init()'s
     * duration), acting as an audible boot chime, not a silent GPIO
     * config. Added 2026-07-16 per explicit report that this port's
     * buzzer never actually sounded at boot (buzzer_rt1062_init() only
     * configures the pin -- nothing had ever called buzzer_on() here).
     * Turned off below, right after this function's program_init()-
     * equivalent work (STORAGE/DISPLAY/NRF_SPI/TIME_SYNC/BMS) finishes,
     * matching the original's own `program_init(); ...Buzzer Off` --
     * NOT the whole rest of app_init() (ENET/TCP/GPS/etc), which in the
     * original is the REST of main() after program_init() returns, by
     * which point the original had already turned the buzzer off. */
    buzzer_rt1062_init();
    buzzer_on();

    /* Position CONFIRMED from the original, ACTIVERFID_V1.02_UHF.c
     * line 3456, main(): `BitWrPortI(PBDR,&PBDRShadow,1,6); //green` --
     * the very next line after Buzzer On, same position, before the
     * 2-second boot delay. Added 2026-07-17 per explicit request for
     * the button LED's full test behavior (RED on boot, solid GREEN
     * once time sync is first achieved, GREEN/off blink while reading
     * -- see the sync/blink transitions in app_loop.c's DS3231 rollover
     * block, and the stop-reading solid-green in app_pc_dispatch.c's
     * app_uhf_reader_control()). RED here (not the original's own
     * green) is a deliberate, explicitly-instructed test refinement --
     * see button_led_rt1062.h's own doc comment for why. */
    button_led_rt1062_init();
    button_led_red();
#endif

    /* CONFIRMED from the original, ACTIVERFID_V1.02_UHF.c line 3457,
     * main(): `msDelay(2000);    // trying to fix boot up lock` -- runs
     * right after the buzzer is switched on and BEFORE `LoadSettings();
     * program_init();`, i.e. before ANY peripheral on the board is
     * touched, not just the display. Per explicit instruction: this
     * gives every peripheral (display, GPS, nRF, BMS chips, etc, all
     * powering up alongside the RT1062 itself) time to complete its own
     * boot before this code starts talking to any of them -- not a
     * display-specific fix. This is not a guessed value -- it's the
     * original author's own fix for a real boot-up lock. Placed at the
     * very top of app_init(), before any subsystem init (including
     * storage/settings load, which the original also runs after this
     * delay), to match. */
    SDK_DelayAtLeastUs(2000000U, SystemCoreClock);

    /* Was `checkInterval = MS_TIMER + 2000; checkInterval2 = MS_TIMER + 15000;`
     * -- battery status check first fires 2s after boot, GPS signal
     * check first fires 15s after boot (both then repeat on their own
     * intervals thereafter -- see BATTERY_CHECK_MS/GPS_SIGNAL_CHECK_MS
     * in app_loop.c, which use different repeat intervals than these
     * initial-delay values, matching the original's own asymmetry
     * between first-fire and steady-state timing).
     *
     * MOVED HERE 2026-07-16, from the very end of this function, per
     * explicit report that the satellite tank appears noticeably later
     * relative to PPS than on the original hardware. The 15s/30s
     * schedule itself is unchanged/still faithful -- what moved is WHEN
     * the 15-second countdown starts. Previously it was computed after
     * every other driver init in this function had already run
     * (storage, display, BMS, ENET+PHY-preflight-retries, GPS's own
     * newly-retried CFG messages, etc), which on the original's much
     * faster Rabbit boot cost negligible time but on this port's boot
     * can add several real seconds -- meaning "15s after this line
     * runs" was actually "15s after boot plus however long the rest of
     * setup took," not "~15s after power-on" like the original's
     * equivalent (positioned at the same RELATIVE point in its own
     * setup, but on hardware where that point is reached almost
     * immediately). Moved to right after the whole-board startup delay
     * -- as close to power-on as this function gets -- so the
     * 15s/30s countdown's real-world meaning matches the original's
     * more closely, even though this port's overall boot is slower. */
    app->check_interval_ms = systick_ms_now() + 2000u;
    app->check_interval2_ms = systick_ms_now() + 15000u;

#if APP_ENABLE_BOARD_IO
    /* buzzer_rt1062_init() MOVED 2026-07-16 to run before the startup
     * delay, alongside buzzer_on() -- see that call site's own comment.
     * uhf_event_cb()/process_uhf_read_buzzer() (see app_loop.c) still do
     * the ongoing on/off writes for actual beep events once this
     * boot-chime period ends (buzzer_off(), below, after this function's
     * program_init()-equivalent work). */

    /* Was implicit in the Rabbit's port setup (PB4 configured for
     * output) -- confirmed now via the GENIE_SYSTEM touch-event
     * handler's PB4 writes (see app_pc_dispatch.c's
     * app_uhf_active_mode_toggle()), plus the confirmed companion
     * READER_PWR pin (both pins always move together, per explicit
     * instruction). Default state at boot (both pins => reader off)
     * matches a safe power-up default; the actual on/off control
     * happens via reader_power_set(), not here. */
    reader_shutdown_rt1062_init();

    /* REAL BUG FOUND AND FIXED 2026-07-17: reader_shutdown_rt1062_init()
     * just above unconditionally re-initializes GPIO3 pin 17 (READER_PWR
     * -- the button LED's pin 2, see button_led_rt1062.h) back to its
     * OWN default level (LOW) as a side effect of GPIO_PinInit(), same
     * as it always has for its original reader-power-control purpose --
     * but since that pin is now ALSO the LED's pin 2, this would
     * silently stomp whatever button_led_red()/_green()/_off() had
     * already set it to at the top of this function, back to LOW,
     * every single boot. Harmless right now only because the boot LED
     * state (RED) also happens to want pin2=LOW -- but would silently
     * corrupt a GREEN or OFF state the moment this ordering shifted, or
     * if this function's own early call ever changed. Re-asserting the
     * LED's boot state here, after the pin that stomps it, is more
     * robust than relying on the two states never diverging by luck. */
    button_led_red();

    /* Confirmed active high, tied to reader start/stop -- see
     * app_pc_dispatch.c's app_uhf_reader_control(). */
    fan_rt1062_init();
#endif /* APP_ENABLE_BOARD_IO */

    /* Shared LPI2C1 bus transfer handle -- must run before either
     * ds3231_rt1062_init() (APP_ENABLE_TIME_SYNC) or bms_init()
     * (APP_ENABLE_BMS) below, since both use lpi2c1_bus_transfer() now.
     * Unconditional, NOT flag-gated -- reverted 2026-07-15 after a real
     * hang on hardware traced to this exact gate: peripherals.c's own
     * LPI2C1_init() (Config-Tools generated, itself unconditional,
     * unaffected by this project's bringup_config.h) already calls
     * LPI2C_MasterTransferCreateHandle() with a NULL callback,
     * regardless of any flag here -- so the LPI2C1 interrupt path is
     * ALWAYS armed. This call re-registers that same handle with a
     * real callback; skipping it left the NULL-callback registration
     * as the only thing servicing the interrupt, which on real
     * hardware (this exact bus carries real DS3231/MP2731/MAX17303
     * traffic in every other build this session) produced a hard hang
     * -- the debugger caught it inside LPI2C1_DriverIRQHandler(),
     * consistent with an interrupt that never got properly serviced/
     * cleared. Does NOT initialize the bus itself -- peripherals.c's
     * LPI2C1_init() (via BOARD_InitPeripherals()) already does that. */
    lpi2c1_bus_rt1062_init();

    /* Was LoadSettings()'s first-boot default-population path. These
     * are always applied first; if APP_ENABLE_STORAGE is on and a
     * valid persisted settings file exists (see the storage-mount
     * block below), settings_store_load() overwrites these with the
     * saved values -- so this block is really "defaults, pending
     * being overridden by whatever was actually saved last." */
    app->settings.reader_power = 80;
    app->settings.channel = 0;
    app->settings.time_zone = 8;
    app->settings.add30 = 0;
    app->settings.beeper = 1;
    app->settings.brightness = 15;
    app->settings.output_type = OUTPUT_DEC;
    app->uhf_mode = 1;
    app->rwr_state = RWR_STOPPED;
    app->gprs_wait_time_ms = 1000;
    /* CRITICAL FIX 2026-07-16, found via the same exhaustive audit that
     * caught the missing SetRabbitIP() call above: `Settings.useDHCP =
     * 0;` and `Settings.RabbitIP[] = {192,168,1,90};` are the original's
     * OWN real first-boot defaults (ACTIVERFID_V1.02_UHF.c lines
     * 2527/2529 -- confirmed, not guessed). Neither was ever explicitly
     * set here; use_dhcp happened to come out right via memset's zero,
     * but rabbit_ip did NOT -- it silently defaulted to 0.0.0.0. That
     * was harmless while nothing ever read it, but now that
     * enet_lwip_rt1062_apply_network_settings() (see the APP_ENABLE_TCP
     * block below) actually APPLIES this value to the netif at every
     * boot, a genuinely fresh device (or one where settings.dat fails
     * validation) would have configured itself with IP 0.0.0.0 --
     * effectively no usable network at all. Fixing this in the SAME
     * change that introduced the dependency, not as a follow-up. */
    app->settings.use_dhcp = 0;
    app->settings.rabbit_ip[0] = 192;
    app->settings.rabbit_ip[1] = 168;
    app->settings.rabbit_ip[2] = 1;
    app->settings.rabbit_ip[3] = 90;
    /* Was `Settings.SendDataToRemoteServer = 1;` (line 2535) -- also
     * found missing by the same audit; memset's 0 default would have
     * silently disabled remote data sending by default, opposite of
     * the original's real intent. GPRSServerIP1/2/3 and RabbitGateway/
     * RabbitDNS are correctly memset to {0,0,0,0} already -- confirmed
     * those specific fields' real defaults ARE all-zero in the
     * original (lines 2539-2543), unlike RabbitIP above. */
    app->settings.send_data_to_remote_server = 1;
    /* TEMPORARILY REVERTED to the original's own real default
     * (`Settings.AutoSetGPSTime = 0;`, ACTIVERFID_V1.02_UHF.c line 2547)
     * 2026-07-16. Was deliberately set to 1 earlier this same session
     * per explicit instruction ("Set time by GPS is a persistent
     * setting... I do not want to have to toggle it each flash"), but
     * that surfaced a real, severe consequence: with PVT polls still
     * never succeeding (separate, still-open investigation), GPS
     * auto-sync retries on EVERY PPS edge (~1x/second, faithful to the
     * original's own `if(!set_time && Settings.AutoSetGPSTime)` block),
     * each attempt blocking ~1100ms -- close enough to the PPS period
     * that the main loop stayed almost permanently blocked in GPS
     * polling, starving touch/display/TCP servicing entirely. Device
     * became unreachable via the touchscreen (the very toggle that
     * would have disabled this at runtime), leaving a code fix + reflash
     * as the only way out. Revert back to 1 once PVT actually succeeds
     * -- the persistence request itself still stands, just deferred
     * until the feature is safe to leave on by default. */
    app->settings.auto_set_gps_time = 0;

    /* Was `iGPRSLastProcessTime = MS_TIMER;` -- gives the network stack
     * a few seconds before Remote_Process() first tries anything,
     * matching the original's "wait 5 seconds before trying remote
     * procedures" comment (the actual wait is gprs_wait_time_ms itself,
     * not 5000 -- the original's comment was aspirational/stale even
     * there, since gprs_wait_time was set to 1000 a few lines below it
     * in the source; preserved faithfully rather than "corrected" to
     * match the comment instead of the code). */
    app->gprs_last_process_time_ms = systick_ms_now();
    app->last_status_broadcast_ms = systick_ms_now();

#if APP_ENABLE_STORAGE
    /* MOVED HERE 2026-07-16 -- was previously at the very end of this
     * function, after nRF/BMS/TCP/GPS/display/system-mode-branch had
     * already run using nothing but the hardcoded defaults just above.
     * The original's ordering is `LoadSettings(); program_init();` at
     * the very top of main() (ACTIVERFID_V1.02_UHF.c line 3458) --
     * settings are loaded from Rabbit's userblock BEFORE program_init()
     * does anything hardware-facing, and MountNANDLogFile() itself runs
     * near the very start of program_init() (line 2657), well before
     * the MP2731/MAX17303/DS3231 writes, TCP setup, or any nRF SPI
     * command. This port's real settings persistence (settings_store.h)
     * only exists inside the SAME littlefs filesystem as the log
     * (no RT1062 equivalent of a separate userblock region), so mount
     * must happen before settings load can happen at all here -- but
     * the LATE position this used to have meant several real hardware
     * pushes upstream (nRF channel push under APP_ENABLE_NRF_SPI, the
     * system-mode branch below choosing comms_NRF(0x0C) vs
     * (0x0A)+(0x03), any future static-IP/DHCP config) were reading
     * whatever the hardcoded defaults said, NOT whatever was actually
     * saved to flash -- a real, if previously dormant, bug (dormant
     * because APP_ENABLE_STORAGE has been off for every hardware pass
     * so far in this staging sequence; would have silently misapplied
     * saved settings the moment it was enabled, with nothing crashing
     * to reveal it). Moved to run immediately after the hardcoded
     * defaults and before anything else touches app->settings.
     *
     * Mount storage -- was nand_log_flash_qspi_get_config() against an
     * MCUboot-partition-aware flash_offset (see
     * nand_log_flash_qspi.h's now-superseded header comment): that
     * offset can't be determined until MCUboot is actually set up
     * (flash_partitioning.h doesn't exist yet -- see
     * OTA_MCUBOOT_INTEGRATION.md), so guessing one would violate this
     * port's own "never guess at hardware facts" rule. Using
     * lfs_mflash.c's LittleFS_config instead: NXP's own mflash driver
     * (adapted from the real EA SDK's littlefs_shell example),
     * CONFIRMED WORKING on real hardware (full mount/format/write/
     * read-back round trip) at a fixed 1MB-in offset that deliberately
     * does NOT coordinate with MCUboot slots yet (see lfs_mflash.h) --
     * revisit this once MCUboot partitioning actually exists. No
     * dependency on anything earlier in this function -- QSPI/FlexSPI
     * flash is independent of I2C/SPI/UART/ENET, and clocks/pins for it
     * are already configured by main() (BOARD_InitBootClocks() etc.)
     * before app_init() is ever called. */
    if (lfs_storage_init(&LittleFS_config) == kStatus_Success) {
        /* TODO PLACEHOLDER, not a confirmed value: log rotation
         * threshold in bytes, passed to nand_log_check_percent_full()/
         * nand_log_should_auto_reset() elsewhere. LittleFS_config's
         * partition is 1MB total (LITTLEFS_BLOCK_COUNT * MFLASH_SECTOR_SIZE,
         * see lfs_mflash.h); 900000 leaves headroom below that for
         * littlefs's own metadata plus the separate settings_store.h
         * file living in the same filesystem. No original-firmware
         * source for the real threshold was available to port
         * faithfully -- adjust if you have (or find) that value. */
        nand_log_mount(&app->log, &LittleFS_config, "/ACTIVERFID.LOG", 900000u);
        nand_log_get_last_log_id(&app->log, &app->last_log_id);
    }

    /* Was LoadSettings() reading from Rabbit's userblock region -- now
     * reads from a small file in the SAME littlefs filesystem as the
     * log, once the mount above is actually wired in. Falls back to
     * the hardcoded defaults already set above if no valid settings
     * file exists yet (first boot) or validation fails (corrupt, or
     * from an incompatible firmware version's layout -- see
     * settings_store.h). */
    if (app->log.mounted) {
        int loaded = settings_store_load(&app->log.lfs, &app->settings);
        /* PRINTF silenced 2026-07-21, per explicit request ("printf on
         * ethernet comms only after boot"). */
        if (!loaded) {
            /* Not found/invalid -- the defaults set above already
             * apply; save them now so this device has a valid
             * settings file from its very first boot, rather than
             * only after the first setting change. */
            settings_store_save(&app->log.lfs, &app->settings);
        }
    }
#endif
#if APP_ENABLE_DISPLAY
    /* Was genieBegin() -- confirms the display link is alive before
     * trying to write anything to it. The ORIGINAL BEHAVIOR still isn't
     * gated on this return value (matches the original's own lack of a
     * hard failure path here -- main() proceeded regardless of
     * genieBegin()'s result, relying on displayDetected's own internal
     * recovery/auto-ping logic to pick up the connection later if it
     * wasn't ready yet at this exact moment) -- this just OBSERVES and
     * prints it, added 2026-07-15 after a real hardware pass where a
     * silently-undetected display would have been indistinguishable
     * from a working-but-quiet one (no touches yet). */
    {
        int detected = display_init();
        /* PRINTF silenced 2026-07-21, per explicit request ("printf on
         * ethernet comms only after boot"). */
        (void)detected;
    }
    display_show_splash("Starting up");
#endif
#if APP_ENABLE_NRF_SPI
    /* Was the nRF52833 SPI transport init + comms_NRF(0x0E)/comms_NRF(0x0D)/
     * comms_NRF(0x0A) sequence near the top of main(). nrf_spi_transport_rt1062_init()
     * returns the transport by value with no failure path exposed at
     * that level (matches the original, which never checked for SPI
     * init failure either) -- if the physical link is dead, the
     * get-fw-version call just below will time out and report it. */
    app->nrf_transport = nrf_spi_transport_rt1062_init();

    /* GPS BUS-PRIMING STEP -- found in the original Dynamic C main(),
     * immediately before comms_NRF(0x0E):
     *
     *   CS2_ENABLE; msDelay(1); SPIRead(&randomstr, 20); CS2_DISABLE;
     *   msDelay(200); comms_NRF(0x0E);
     *
     * with the original author's own comment: "seem to have to do comms
     * with GPS chip on SPI for proper SPI comms to nrf chip" -- confirmed
     * on real hardware 2026-07-14 to be genuinely required: without this,
     * fw_version always reads the nRF SPIS peripheral's own idle-default
     * byte (no real reply ever armed); with it, fw_version reads
     * correctly. Deliberately does NOT depend on APP_ENABLE_GPS -- the
     * nRF SPI link needs this regardless of whether the GPS subsystem
     * itself is separately enabled, so this uses neo_m8t_transport_rt1062_init()'s
     * raw CS/transfer primitives directly rather than the full GPS
     * transport/UBX-parsing path (gps_stub.c), which may not be brought
     * up at all in this build. nRF's own CS is already deasserted by
     * nrf_spi_transport_rt1062_init() just above, so no separate GPIO
     * write is needed here to avoid bus contention during this read
     * (unlike the bring-up test, where this ran before that init call). */
    {
        neo_m8t_transport_t gps_prime = neo_m8t_transport_rt1062_init();
        uint8_t randomstr[20];

        gps_prime.cs_enable(gps_prime.ctx);
        gps_prime.delay_ms(gps_prime.ctx, 1);
        gps_prime.transfer(gps_prime.ctx, NULL, 0, randomstr, sizeof(randomstr));
        gps_prime.cs_disable(gps_prime.ctx);
        gps_prime.delay_ms(gps_prime.ctx, 200);
    }

    {
        uint8_t fw_version = 0;
        nrf_spi_status_t fw_status;
        /* Was `comms_NRF(0x0E); sprintf(version_str,...); genieWriteStr(...)` --
         * the GENIE display write is gated separately below; this just
         * confirms the link is alive. Read ONCE here and used immediately
         * -- do not re-query later, since tx_buf[1] on the nRF side is a
         * shared byte reused for other purposes (battery percent, etc.)
         * moments after boot; confirmed on real hardware that repeat
         * queries show unrelated data, not fw_version, within tens of
         * milliseconds.
         *
         * PRINTF added 2026-07-17 -- was read into `fw_version` and then
         * silently discarded with a `TODO: surface fw_version somewhere
         * (debug UART print)` left directly in this comment; user
         * reported not seeing anything on the console, which turned out
         * to be exactly this gap, not a hardware/link failure. Prints
         * both the status and the raw byte -- a `0xBB`/`0xDD` byte with
         * NRF_SPI_OK status means the link responded but got the SPIS
         * idle-default (no real reply armed, e.g. GPS-priming didn't
         * work), while any other status means the transfer itself
         * failed (see nrf_spi_protocol.h for the status codes). */
        fw_status = nrf_spi_get_fw_version(&app->nrf_transport, &fw_version);
        /* PRINTF silenced 2026-07-20, per explicit request ("clean up
         * this shit, I dont want to see nrf printf anymore"). fw_status/
         * fw_version are still used below (splash-string fallback
         * logic), just no longer printed here. */

#if APP_ENABLE_DISPLAY
        /* Was `sprintf(version_str, "RCM %02d.%02d nRF%d E+", ...);
         * genieWriteStr(GENIE_SPLASH_STR, version_str);`
         * (ACTIVERFID_V1.02_UHF.c line 3476-3477) -- never ported, a real
         * splash-message gap. A bad/incomplete read doesn't mean the nRF
         * is faulty -- it's a known debug-probe boot-timing artifact (the
         * nRF only answers 0x0E within a window after its own boot); this
         * project has directly observed BOTH `status=0,byte=0x00` (during
         * the 2026-07-17 LPSPI3 mode/baud investigation) and the SPIS
         * idle-default byte (0xBB/0xDD) as "didn't really answer" results
         * on this exact link, so all three are treated as invalid here --
         * don't block/retry, just show a placeholder instead of a
         * specific-but-possibly-wrong number. RCM version left as "-.-"
         * -- no confirmed _FIRMWARE_VERSION_ constant exists in this port
         * yet, same gap already flagged in app_genie_dispatch.c's
         * GENIE_FORM_OTHER handler. */
        {
            char version_str[32];
            int fw_valid = (fw_status == NRF_SPI_OK) && (fw_version != 0x00)
                         && (fw_version != NRF_SPI_POLL_IGNORE_SENTINEL)
                         && (fw_version != 0xDD);
            if (fw_valid) {
                snprintf(version_str, sizeof(version_str), "RCM -.- nRF%d E+", fw_version);
            } else {
                snprintf(version_str, sizeof(version_str), "RCM -.- nRF-- E+");
            }
            display_show_splash(version_str);
        }
#endif
    }

    /* Was `//comms_NRF(0x0D);` at this exact boot position (line 3513) --
     * FIDELITY FIX, 2026-07-18: the original ships this call permanently
     * COMMENTED OUT, both here and at its other potential site (line
     * 3819, also commented out) -- real battery % comes from the
     * MAX17303 fuel gauge (see bms_init.h/APP_ENABLE_BMS), not the nRF.
     * This port previously called it unconditionally, exercising a code
     * path the original never actually enabled. Removed to match. If a
     * live nRF-side battery report is ever wanted, it needs deliberately
     * re-adding, not carrying forward as an unexamined default. */

    /* The boot-time comms_NRF(0x0A)/comms_NRF(0x03)/comms_NRF(0x0C)
     * sequence does NOT belong here -- FIDELITY FIX, 2026-07-18: the
     * original's real boot sequence (line 3518-3524) only sends these
     * much later, gated on Settings.System, AFTER genieActivateForm()/
     * updateGenie_Main() -- this port already has that exact gated
     * block further down in this function (search for
     * "app->settings.system" -- was comms_NRF(0x0C) for System==1,
     * comms_NRF(0x0A)+comms_NRF(0x03) for System==0). An unconditional
     * nrf_spi_set_channel() call was previously placed HERE instead,
     * which doesn't correspond to anything in the original's boot
     * sequence at this position -- removed. See that later block for the
     * real, now-fixed 0x0A call. */
#endif
#if APP_ENABLE_TIME_SYNC
    /* Was SetVectExtern(1, DS3231_isr) + I1CR trigger config */
    if (ds3231_rt1062_init() != 0) {
        return -1;
    }
    /* TODO: WrPortI(I1CR,...) equivalent -- trigger-input IRQ config.
     * The original's trigger branch inside DS3231_isr() was itself
     * entirely commented out (see the RTC/GPS porting notes from
     * earlier in this conversation), so there may be nothing live to
     * port here regardless of Settings.TriggerOn. */
#endif
#if APP_ENABLE_BMS
    /* Was program_init()'s MP2731/MAX17303 setpoint section, right
     * after DS3231 setup in the original (both on the same I2C bus).
     * See bms_init.c for the byte-for-byte ported sequence. */
    bms_init(&app->board_version);
#endif

#if APP_ENABLE_BOARD_IO
    /* CONFIRMED root cause found 2026-07-17 for "reader is not reading,
     * fails startup init" (every single UHF command getting 0 reply
     * bytes): this block -- the reader power-up/down GPIO write keyed
     * on Settings.System -- was ENTIRELY MISSING from this port.
     * ACTIVERFID_V1.02_UHF.c lines 2576-2582, the LAST statement inside
     * program_init() (i.e. runs on EVERY boot, not just on a live
     * GENIE_SYSTEM touchscreen toggle):
     *
     *   if(Settings.System){
     *       BitWrPortI(PBDR,&PBDRShadow,0,4);    // PB4 low = reader on
     *       BitWrPortI(PADR,&PADRShadow,1,0);    // PA0 high = reader power on
     *   }else{
     *       BitWrPortI(PBDR,&PBDRShadow,1,4);    // reader off
     *       BitWrPortI(PADR,&PADRShadow,0,0);    // reader power off
     *   }
     *
     * -- exactly `reader_power_set(Settings.System)` in this port's own
     * terms (reader_shutdown_rt1062.c's reader_shutdown_set()/
     * reader_pwr_set() pin polarities were confirmed to match this
     * exactly when they were first ported). The ONLY place this port
     * previously called reader_power_set() was
     * app_uhf_active_mode_toggle() (app_pc_dispatch.c's GENIE_SYSTEM
     * handler) -- fired on a LIVE touchscreen toggle, never at boot.
     * reader_shutdown_rt1062_init() leaves both pins in the reader-off
     * state as a safe default (see its own comment) -- so on any boot
     * where Settings.System==1 was already persisted from a previous
     * session (the normal case once a station is configured, since
     * this doesn't reset on every reflash), the reader stayed powered
     * OFF the whole time unless the user happened to re-toggle System
     * mode on the touchscreen THIS session before pressing start. That
     * fully explains the trace: every command in the config sequence
     * got a clean 0-byte reply, not corrupted data -- the reader
     * hardware was simply never powered up in the first place. Placed
     * here to match the original's position (the last statement of
     * program_init(), after settings are loaded/validated/saved --
     * already true by this point, see the storage-load block earlier
     * in this function) and right before this port's own
     * program_init()-equivalent boundary marker (buzzer_off(),
     * immediately below). */
    reader_power_set(app->settings.system);

    /* CONFIRMED from the original, ACTIVERFID_V1.02_UHF.c line 3460,
     * main(): `BitWrPortI(PBDR,...,0,2); //Buzzer Off`, immediately
     * after `program_init()` returns -- see buzzer_on()'s own comment
     * for the full story. STORAGE/DISPLAY/NRF_SPI/TIME_SYNC/BMS above
     * are this port's equivalent of program_init()'s content; everything
     * below this point (ENET/TCP, GPS config, GPRS, display activate
     * MAIN) corresponds to the REST of main() after program_init()
     * returns, where the original's buzzer is already off. */
    buzzer_off();
#endif

#if APP_ENABLE_TCP
    /* ENET/PHY/lwIP bring-up -- was entirely missing until 2026-07-14
     * (bringup_config.h's own Stage 2 comment flagged this as "outside
     * this port's scope" until now). Must run BEFORE any
     * tcp_lwip_*_open() call below, which all need a working netif to
     * already exist. Non-blocking -- see enet_lwip_rt1062.h's header
     * comment; does not wait for link-up or a DHCP lease before
     * returning. Also populates app->mac_address, used independently by
     * the UDP discovery responder and GPRS batch sender below. */
    app->enet_available = (enet_lwip_rt1062_init(app->mac_address) == 0);

    /* RESTORED 2026-07-21, per explicit report ("DHCP on/off is a
     * persistent setting. It is not staying off if I set it to off and
     * reboot"). This call was originally added, then REVERTED 2026-07-16
     * ("go back to what worked") when app_init() started hanging inside
     * enet_lwip_rt1062_init() the same session STORAGE was newly enabled
     * -- at the time it wasn't confirmed whether this call or STORAGE's
     * own ordering was the cause, so it was pulled out to restore a
     * known-working boot while the real hang got investigated separately.
     * That investigation found the ACTUAL cause days later: debug-probe
     * interference with GPIO1 9/10 (ENET_RST/ENET_INT, shared with
     * JTAG_TDI/TDO) during enet_phy_hw_reset() -- completely unrelated to
     * this call, and now permanently fixed by physically cutting the TDI
     * trace to the debug header (see project memory). STORAGE was also
     * independently confirmed innocent. Nothing was ever found wrong with
     * this call itself -- it was reverted on suspicion, not evidence, and
     * the suspicion turned out to be misplaced. Without it,
     * enet_lwip_rt1062_init()'s own unconditional dhcp_start() means
     * app->settings.use_dhcp/rabbit_ip/rabbit_gateway -- correctly saved
     * and correctly loaded from flash -- were never actually applied to
     * the netif at boot; DHCP restarted every single boot regardless of
     * a saved static-IP preference. This is the real SetRabbitIP()
     * equivalent (ACTIVERFID_V1.02_UHF.c:2651, program_init()'s own
     * unconditional call), now finally restored to match. */
    if (app->enet_available) {
        enet_lwip_rt1062_apply_network_settings(app->settings.use_dhcp,
                                                 app->settings.rabbit_ip,
                                                 app->settings.rabbit_gateway);
    }

    /* Was TCPIPOpenSockets() -- SKIPPED entirely (not fatal) if the
     * PHY never came up, added 2026-07-16 alongside
     * enet_lwip_rt1062_init()'s new non-fatal failure return. Opening
     * a TCP listener on a netif that was never added would be
     * meaningless (and likely a NULL/uninitialized-netif fault) if
     * enet_lwip_rt1062_init() bailed out early -- app_loop.c's
     * per-iteration TCP block checks the same app->enet_available flag
     * before touching any of these, so leaving them unopened here is
     * safe, not just deferred. */
    if (app->enet_available) {
        if (tcp_lwip_listener_open(&app->tcp_listener, 23) != 0) {
            return -1;
        }
        if (tcp_lwip_reset_socket_open(&app->reset_transport, 8001) != 0) {
            return -1;
        }
        if (tcp_lwip_udp_discovery_open(&app->discovery_transport, 2000) != 0) {
            return -1;
        }
    }
#endif

    /* Was SetVectExtern(0, my_isr) + I0CR PPS IRQ config -- GPS PPS
     * edge detection. Now wired: gps_configure_timepulse() below (via
     * gps_stub.c -> neo_m8t_transport_rt1062_init()) sets up the PPS
     * GPIO interrupt as a side effect of initializing the GPS SPI
     * transport; app_loop.c's process_time_sync() reads the ISR's
     * state each iteration via neo_m8t_gps_get_pps_state(). */

    /* Battery percent may already have been read above under
     * APP_ENABLE_NRF_SPI (a stale hangover source per project memory --
     * MAX17303 via BMS is the real one). MUST run before
     * app_genie_update_main() below, same reasoning as the DS3231 read
     * just below -- added 2026-07-16 per explicit request ("bat% bar
     * and value need to update fast before loop"): without this, the
     * Genie battery gauge/digits painted a stale 0% (or whatever the
     * NRF_SPI read left behind) for the first BATTERY_CHECK_MS/2000ms
     * of every boot, since process_periodic_checks()'s own read of
     * this doesn't fire until 2 seconds into the main loop. No-op
     * unless APP_ENABLE_BMS is on AND board_version>=32 (see
     * app_update_battery_percent()'s own doc comment in app_context.h). */
    app_update_battery_percent(app);

#if APP_ENABLE_TIME_SYNC
    /* MUST run before app_genie_update_main() below -- see that block's
     * own comment. Was RTC_Get() being called INSIDE the original's
     * updateGenie_Main() itself (ACTIVERFID_V1.02_UHF.c line 1470,
     * `mytime = RTC_Get();`, right before writing the hour/min digits)
     * -- the original always self-reads a fresh RTC value at display
     * time, never relying on some earlier cached read. This port
     * split "read the RTC" and "display it" into separate functions
     * (app->current_time as the shared cache, matching how
     * process_time_sync()'s rollover-driven refresh also just reads
     * the cache rather than re-querying I2C every tick) -- correct
     * architecture, but it meant the ORDER these two blocks run in now
     * matters, where the original had no such ordering dependency at
     * all. FOUND AND FIXED 2026-07-16: this used to run AFTER the
     * display block below, so app_genie_update_main() at boot was
     * populating the MAIN screen from app->current_time while it was
     * still the memset() zero-default -- DS3231 has a battery-backed
     * RTC and should show its real retained time immediately on
     * power-up, not 00:00/epoch-zero until the first TIMEPULSE
     * rollover corrects it a second or so later. */
    if (ds3231_rt1062_read(&app->current_time) != 0) {
        return -1;
    }
    app->last_touch_time_s = (uint32_t)rtc_datetime_to_epoch(&app->current_time);
#endif

#if APP_ENABLE_DISPLAY
    /* Was `genieActivateForm(GENIE_FORM_MAIN); updateGenie_Main();` --
     * the real original boot sequence, right before entering the main
     * loop. This call site was previously an unresolved placeholder
     * (`display_activate_form(0)` -- form 0 is GENIE_FORM_SPLASHSCREEN,
     * not MAIN) that left the display sitting on the splash screen
     * forever, which has no touchable widgets at all -- confirmed by
     * the user on real hardware (splash detected fine, but nothing to
     * touch). app_genie_update_main() is the direct port of
     * updateGenie_Main() (see app_genie_dispatch.c), so this now
     * matches the original exactly: activate MAIN, then refresh its
     * widgets from current settings/state. Now runs AFTER the
     * APP_ENABLE_TIME_SYNC block above (see that block's comment) so
     * app->current_time is real DS3231 data, not the zero default,
     * the very first time this paints the screen. */
    /* Only mark display_main_shown on an actual ACK (return value 1),
     * not merely "the display was online" -- display_activate_form()
     * can still time out (up to genie_cmd_timeout==1250ms) even when
     * display_detected is true, e.g. if this call races with an
     * in-flight auto-ping read. Checking display_is_online() after the
     * call (an earlier version of this fix) had the same "mark done
     * regardless of actual success" bug this whole mechanism exists to
     * avoid -- see app_loop.c's per-iteration retry, which now also
     * only trusts the real return value. If this boot-time attempt
     * fails for any reason (not detected yet, NAK, or timeout), that
     * retry picks it up on its own cooldown. */
    if (display_activate_form(GENIE_FORM_MAIN) == 1) {
        app_genie_update_main(app);
        app->display_main_shown = 1;
    }
#endif

    /* MOVED HERE 2026-07-16, from right after the TCP listeners block
     * (before battery_percent/TIME_SYNC-read/DISPLAY-activate-MAIN),
     * per explicit hard requirement: "main screen needs to be shown in
     * 4 seconds." gps_configure_timepulse() sends 6 UBX CFG messages,
     * EACH with up to a 1100ms no-retry timeout if it doesn't ACK (see
     * process_ubx_message()) -- this session's own diagnostics measured
     * a well-under-100% single-attempt success rate for these writes,
     * so a bad boot could previously cost up to ~6.6 seconds here ALONE
     * before app_init() ever reached the display-activation code,
     * blowing the 4-second budget outright. This is a DELIBERATE
     * deviation from the original's own ordering (`set_UBX()` before
     * `genieActivateForm(MAIN)`) -- not a guess, an explicit instruction
     * overriding sequence-fidelity for this specific case, same as this
     * session's other confirmed-instruction-driven changes. GPS/GPRS
     * have no data dependency on display activation (gps_configure_
     * timepulse() doesn't touch the display at all -- the satellite
     * tank is a separately-timed periodic check in app_loop.c), so
     * this is safe to reorder. */
#if APP_ENABLE_GPS
    /* CONFIRMED from the original, ACTIVERFID_V1.02_UHF.c line 3479,
     * main(): `msDelay(2500);` -- a real, explicit 2.5-second delay,
     * positioned right after the nRF-fw-version splash/contrast write
     * and BEFORE fan-off, DS3231 ISR install, TCPIPOpenSockets(),
     * httpc_init(), and finally set_UBX() (line 3504). This is a
     * discrete, position-anchored delay in the original's own design,
     * not a "wait until N ms since boot" calculation -- replaces an
     * earlier version of this fix (2026-07-16) that invented a
     * systick-based since-boot gate from a rough paraphrase of the
     * requirement instead of checking the actual source first, and
     * used the wrong value (2000ms, not the source's real 2500ms).
     * The original's other work in this exact window (nRF fw-version
     * query/splash, fan on/off, TCPIPOpenSockets(), httpc_init()) isn't
     * replicated here -- NRF_SPI is currently off, and
     * TCPIPOpenSockets()/httpc_init()'s equivalents already ran earlier
     * in this port's own boot order (see the TCP block above) -- this
     * only ports the confirmed delay value itself, at the position
     * closest to its original intent (immediately before set_UBX()'s
     * equivalent). */
    SDK_DelayAtLeastUs(2500000U, SystemCoreClock);
    gps_configure_timepulse(); /* was set_UBX() */
#endif

#if APP_ENABLE_GPRS
    /* TODO: app->gprs_transport = gprs_transport_rt1062_init(); */
    gprs_modem_init(&app->modem, &app->gprs_transport);
    if (!app->settings.remote_type) {
        gprs_modem_toggle(&app->modem, 0);
    }
#endif

    /* With APP_ENABLE_TIME_SYNC off, app->current_time stays
     * zero-initialized (memset above) -- downstream code that reads it
     * (record timestamps, display) will show epoch/zero values rather
     * than crashing, which is the point of staged bring-up: everything
     * still runs, just with an obviously-wrong clock until you enable
     * this stage. */

    if (app->settings.system) {
        app->program_state = APP_STATE_IDLE;
#if APP_ENABLE_NRF_SPI
        /* Was comms_NRF(0x0C) -- UHF-mode stations keep the nRF's own
         * LF transmitter off, since chip reading happens over the
         * separate UHF UART link, not this SPI link. */
        nrf_spi_transmitter_off(&app->nrf_transport);
#endif
    } else {
#if APP_ENABLE_NRF_SPI
        /* Was comms_NRF(0x0A) -- push the configured channel, sent
         * immediately before comms_NRF(0x03) at this exact position in
         * the original (line 3522-3523). FIDELITY FIX, 2026-07-18: this
         * call was missing here -- an unconditional, unguarded copy of
         * it existed earlier in this function instead, which didn't
         * correspond to anything in the original's real boot sequence
         * at that position. Moved to here, matching source exactly. */
        nrf_spi_set_channel(&app->nrf_transport, app->settings.channel);
        /* Was comms_NRF(0x03) -- Active/LF-mode stations push the
         * configured reader power instead. */
        nrf_spi_set_reader_power(&app->nrf_transport, app->settings.reader_power);
#endif
    }

#if APP_ENABLE_TCP
    /* Was TCPIPOpenSocket_FinishLynx() -- now real, see
     * finish_lynx_protocol.h/app_loop.c's process_finish_lynx_socket().
     * Gated on app->enet_available, added 2026-07-16 -- this is a
     * SECOND, separate #if APP_ENABLE_TCP block from the one earlier in
     * this function (tcp_listener/reset_transport/discovery_transport);
     * missed gating it the first time this flag was added, which meant
     * a boot with ENET unavailable still hit this listener-open call on
     * a netif that was never created, failed, and aborted app_init()
     * entirely via the `return -1` below -- defeating the whole point
     * of making ENET failure non-fatal. See enet_lwip_rt1062_init()'s
     * header comment for the full story. */
    if (app->enet_available) {
        if (tcp_lwip_listener_open(&app->finish_lynx_listener, FINISH_LYNX_PORT) != 0) {
            return -1;
        }
    }
#endif

#if APP_ENABLE_UHF
    /* Was never wired anywhere -- a real, confirmed gap found 2026-07-17
     * before ever flipping this flag on: app->uhf_transport was left at
     * its memset() zero value (every function pointer NULL), since
     * nothing populated it. app_uhf_reader_control()'s uhf_reader_open()
     * calls t->open(...) directly with no NULL check (matches every
     * other transport in this port -- see uhf_reader_open() itself), so
     * the very first UHF_START (touchscreen, PC command, or the
     * abnormal-shutdown auto-resume just below) would have called
     * through a NULL function pointer and hard-faulted. Fixed by
     * populating the transport struct here, once, at boot -- same
     * pattern as nrf_spi_transport_rt1062_init() above.
     * uhf_transport_rt1062_init() itself only wires up function
     * pointers (no hardware touched, confirmed by reading its source),
     * so this is safe to call unconditionally even if UHF reading never
     * actually starts this boot. The real serial-port open still
     * happens later, in uhf_reader_open() (called from
     * app_uhf_reader_control() every time reading starts) -- matches
     * the original's own Open_Reader()-on-every-start behavior, not
     * duplicated here. */
    app->uhf_transport = uhf_transport_rt1062_init();
#endif

#if APP_ENABLE_UHF && APP_ENABLE_STORAGE
    if (app->settings.shutdown_status == 1) {
        app_uhf_reader_control(app, 1);
    }
#endif

    PRINTF("app_init: COMPLETE, entering main loop\r\n");
    return 0;
}
