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
#include "display_stub.h"
#include "gps_stub.h"
#include "civil_time.h"
#include "bringup_config.h"
#include "systick_ms_rt1062.h"
#include "nrf_spi_protocol.h"
#include "nrf_spi_transport_rt1062.h"
#include "settings_store.h"
#include "bms_init.h"
#include "buzzer_rt1062.h"
#include "reader_shutdown_rt1062.h"
#include "fan_rt1062.h"
#include "lpi2c1_bus_rt1062.h"
#include "finish_lynx_protocol.h"
#include <string.h>

int app_init(app_context_t *app)
{
    memset(app, 0, sizeof(*app));

    /* Was implicit in the Rabbit's port setup at the top of
     * program_init() (PB2 configured for output alongside the other
     * port pins there) -- buzzer_rt1062_init() configures the GPIO
     * output; app_beep()/process_beeper_and_dim() (see app_loop.c) do
     * the actual on/off writes once something calls app_beep(). */
    buzzer_rt1062_init();

    /* Was implicit in the Rabbit's port setup (PB4 configured for
     * output) -- confirmed now via the GENIE_SYSTEM touch-event
     * handler's PB4 writes (see app_pc_dispatch.c's
     * app_uhf_active_mode_toggle()), plus the confirmed companion
     * READER_PWR pin (both pins always move together, per explicit
     * instruction). Default state at boot (both pins => reader off)
     * matches a safe power-up default; the actual on/off control
     * happens via reader_power_set(), not here. */
    reader_shutdown_rt1062_init();

    /* Confirmed active high, tied to reader start/stop -- see
     * app_pc_dispatch.c's app_uhf_reader_control(). */
    fan_rt1062_init();

    /* Shared LPI2C1 bus transfer handle -- must run before either
     * ds3231_rt1062_init() (APP_ENABLE_TIME_SYNC) or bms_init()
     * (APP_ENABLE_BMS) below, since both use lpi2c1_bus_transfer() now.
     * Unconditional since either flag alone needs it. Does NOT
     * initialize the bus itself -- peripherals.c's LPI2C1_init() (via
     * BOARD_InitPeripherals()) already does that. */
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
    app->settings.beeper = 1;
    app->settings.brightness = 15;
    app->settings.output_type = OUTPUT_DEC;
    app->uhf_mode = 1;
    app->rwr_state = RWR_STOPPED;
    app->gprs_wait_time_ms = 1000;

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

#if APP_ENABLE_DISPLAY
    /* Was genieBegin() -- confirms the display link is alive before
     * trying to write anything to it. Not gated on the return value
     * (matches the original's own lack of a hard failure path here --
     * main() proceeded regardless of genieBegin()'s result, relying on
     * displayDetected's own internal recovery/auto-ping logic to pick
     * up the connection later if it wasn't ready yet at this exact
     * moment). */
    display_init();
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

    {
        uint8_t fw_version = 0;
        /* Was `comms_NRF(0x0E); sprintf(version_str,...); genieWriteStr(...)` --
         * the GENIE display write is gated separately below; this just
         * confirms the link is alive. TODO: surface fw_version somewhere
         * (debug UART print) until APP_ENABLE_DISPLAY is on. */
        nrf_spi_get_fw_version(&app->nrf_transport, &fw_version);
    }

    {
        uint8_t batt_percent = 0;
        /* Was comms_NRF(0x0D) */
        if (nrf_spi_get_battery_percent(&app->nrf_transport, &batt_percent) == NRF_SPI_OK) {
            app->batt_percent = batt_percent;
        }
    }

    /* Was comms_NRF(0x0A) -- push the configured channel to the nRF */
    nrf_spi_set_channel(&app->nrf_transport, app->settings.channel);
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

#if APP_ENABLE_TCP
    /* Was TCPIPOpenSockets() */
    if (tcp_lwip_listener_open(&app->tcp_listener, 23) != 0) {
        return -1;
    }
    if (tcp_lwip_reset_socket_open(&app->reset_transport, 8001) != 0) {
        return -1;
    }
    if (tcp_lwip_udp_discovery_open(&app->discovery_transport, 2000) != 0) {
        return -1;
    }
#endif

#if APP_ENABLE_GPS
    gps_configure_timepulse(); /* was set_UBX() */
#endif

#if APP_ENABLE_GPRS
    /* TODO: app->gprs_transport = gprs_transport_rt1062_init(); */
    gprs_modem_init(&app->modem, &app->gprs_transport);
    if (!app->settings.remote_type) {
        gprs_modem_toggle(&app->modem, 0);
    }
#endif

    /* Was SetVectExtern(0, my_isr) + I0CR PPS IRQ config -- GPS PPS
     * edge detection. Now wired: gps_configure_timepulse() below (via
     * gps_stub.c -> neo_m8t_transport_rt1062_init()) sets up the PPS
     * GPIO interrupt as a side effect of initializing the GPS SPI
     * transport; app_loop.c's process_time_sync() reads the ISR's
     * state each iteration via neo_m8t_gps_get_pps_state(). */

    /* Battery percent is now read above under APP_ENABLE_NRF_SPI; with
     * that flag off, app->batt_percent stays 0 (harmless -- just shows
     * 0% on any status output until that stage is enabled). */

#if APP_ENABLE_DISPLAY
    display_activate_form(0);
#endif

#if APP_ENABLE_TIME_SYNC
    if (ds3231_rt1062_read(&app->current_time) != 0) {
        return -1;
    }
    app->last_touch_time_s = (uint32_t)rtc_datetime_to_epoch(&app->current_time);
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
        /* Was comms_NRF(0x03) -- Active/LF-mode stations push the
         * configured reader power instead. */
        nrf_spi_set_reader_power(&app->nrf_transport, app->settings.reader_power);
#endif
    }

#if APP_ENABLE_TCP
    /* Was TCPIPOpenSocket_FinishLynx() -- now real, see
     * finish_lynx_protocol.h/app_loop.c's process_finish_lynx_socket(). */
    tcp_lwip_listener_open(&app->finish_lynx_listener, FINISH_LYNX_PORT);
#endif

#if APP_ENABLE_STORAGE
    /* Mount storage. TODO: confirm your actual flash_offset/flash_size
     * split -- this now also depends on MCUboot's flash_partitioning.h
     * (see OTA_MCUBOOT_INTEGRATION.md): littlefs must start AFTER the
     * secondary slot, not just after "the firmware image" as originally
     * noted. */
    {
        struct lfs_config cfg;
        /* TODO: nand_log_flash_qspi_get_config(&cfg, YOUR_OFFSET, YOUR_SIZE); */
        (void)cfg;
        /* nand_log_mount(&app->log, &cfg, "/ACTIVERFID.LOG", YOUR_MAX_SIZE);
           nand_log_get_last_log_id(&app->log, &app->last_log_id); */
    }

    /* Was LoadSettings() reading from Rabbit's userblock region -- now
     * reads from a small file in the SAME littlefs filesystem as the
     * log, once the mount above is actually wired in. Falls back to
     * the hardcoded defaults already set above if no valid settings
     * file exists yet (first boot) or validation fails (corrupt, or
     * from an incompatible firmware version's layout -- see
     * settings_store.h). */
    if (app->log.mounted) {
        if (!settings_store_load(&app->log.lfs, &app->settings)) {
            /* Not found/invalid -- the defaults set above already
             * apply; save them now so this device has a valid
             * settings file from its very first boot, rather than
             * only after the first setting change. */
            settings_store_save(&app->log.lfs, &app->settings);
        }
    }
#endif

#if APP_ENABLE_UHF && APP_ENABLE_STORAGE
    if (app->settings.shutdown_status == 1) {
        app_uhf_reader_control(app, 1);
    }
#endif

    /* Was `checkInterval = MS_TIMER + 2000; checkInterval2 = MS_TIMER + 15000;`
     * -- battery status check first fires 2s after boot, GPS signal
     * check first fires 15s after boot (both then repeat on their own
     * intervals thereafter -- see BATTERY_CHECK_MS/GPS_SIGNAL_CHECK_MS
     * in app_loop.c, which use different repeat intervals than these
     * initial-delay values, matching the original's own asymmetry
     * between first-fire and steady-state timing). */
    app->check_interval_ms = systick_ms_now() + 2000u;
    app->check_interval2_ms = systick_ms_now() + 15000u;

    return 0;
}
