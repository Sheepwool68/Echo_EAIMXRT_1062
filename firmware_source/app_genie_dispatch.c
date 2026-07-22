/*
 * app_genie_dispatch.c
 *
 * See app_genie_dispatch.h. Was myGenieEventHandler()
 * (ACTIVERFID_V1.02_UHF.c) -- ported section-by-section in the same
 * object-dispatch order as the original's if/else chain, reusing every
 * already-built module this port has (settings_store, nrf_spi_*,
 * uhf_reader, nand_log, app_uhf_reader_control/app_uhf_active_mode_toggle,
 * civil_time/rtc_time/ds3231_rt1062) rather than re-deriving any of
 * their logic here.
 *
 * STUBBED, per explicit scope decision (see app_genie_dispatch.h):
 *   - Network IP/DHCP reconfiguration (was UpdateRabbitIP()) -- needs
 *     new lwIP static-IP/DHCP-restart code that doesn't exist yet.
 *   - check_fw()/install_firmware() -- Rabbit-only httpc/buDownload
 *     libraries, superseded by (but not yet wired to)
 *     fw_version_check.h/fw_downloader.h/fw_install_mcuboot.h.
 *   - Toggle_Modem() -- no confirmed body was ever pasted from source;
 *     flagged rather than invented, per this port's "never guess" rule.
 * Each stub is marked TODO at its call site below.
 */

#include "app_genie_dispatch.h"
#include "app_pc_dispatch.h"
#include "bringup_config.h"
#include "settings_store.h"
#include "civil_time.h"
#include "rtc_time.h"
#include "ds3231_rt1062.h"
#include "uhf_reader.h"
#include "nand_log_littlefs.h"
#include "nand_log_logic.h"
#if APP_ENABLE_NRF_SPI
#include "nrf_spi_protocol.h"
#endif
#include "max17303_fuel_gauge_rt1062.h"
#if APP_ENABLE_TCP
#include "enet_lwip_rt1062.h"
#endif
#if APP_ENABLE_GPRS
#include "gprs_modem.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Debug tracing, added 2026-07-20 to diagnose the Network form's LAN IP
 * field still showing 0.0.0.0 after the settings_store_load() corruption
 * fix + version bump -- same pattern already used in uhf_reader.c/
 * neo_m8t_reader.c/nrf_spi_protocol.c: redirect PRINTF to debug_printf()
 * (LPUART5, independent of the SWD/semihosting debug link). This file
 * never had this redirect before -- worth keeping now that it's here. */
#include "debug_console_rt1062.h"
#undef PRINTF
/* SILENCED 2026-07-21, per explicit request ("printf on ethernet
 * comms only after boot"). Was `debug_printf`. Restore if this
 * tracing is wanted again. */
#define PRINTF(...) ((void)0)

/* Was `int BT_ON_TIMES[8] = {0, 1, 2, 3, 5, 8, 12, 24};` (hours) --
 * confirmed constant, only ever indexed by TB_BT_ON's trackbar value
 * (0-7) here, so kept local rather than promoted to a shared header. */
static const int BT_ON_TIMES[8] = {0, 1, 2, 3, 5, 8, 12, 24};

/* Admin PIN, was the literal `6868` compared against atoi(keyboard_string)
 * in the KEYPAD Enter handler -- confirmed constant from source. */
#define APP_GENIE_ADMIN_PIN 6868

static void app_persist_settings(app_context_t *app)
{
    if (app->log.mounted) {
        settings_store_save(&app->log.lfs, &app->settings);
    }
}

/* Was `sprintf(keyboard_string, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3])`,
 * the dotted-quad-to-string direction used to seed keyboard_string when a
 * WinButton opens an IP-entry KEYPAD form. */
static void format_dotted_quad(const uint8_t ip[4], char *out, size_t out_size)
{
    snprintf(out, out_size, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

/*
 * Was parseIP_check()'s strtok loop -- parses app->keyboard_string as a
 * dotted-quad into out_ip[4]. Returns 1 on success (exactly 4 tokens),
 * 0 otherwise (was the `if(i != 4) return 0;` check). Ported as its own
 * strtok-based loop rather than forcing this onto ip_addr_parse.h's
 * dotted-quad-to-uint32_t helper (built for a different call site,
 * ConnectToSocketServer()) -- this needs 4 separate bytes and the
 * original's own algorithm is simple enough to port directly for
 * fidelity.
 */
static int app_genie_parse_ip(const char *str, uint8_t out_ip[4])
{
    char buf[20];
    char *token;
    int i = 0;

    /* Plain strtok(), matching the original's own `strtok(istr, ".")`
     * -- this firmware is single-threaded/bare-metal, so strtok()'s
     * shared static state (the only reason to prefer strtok_r) is a
     * non-issue, and strtok_r isn't declared under this toolchain's
     * -std=c11. */
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    token = strtok(buf, ".");
    while (token != NULL && i < 4) {
        out_ip[i] = (uint8_t)atoi(token);
        token = strtok(NULL, ".");
        i++;
    }
    if (token != NULL) {
        i++; /* more than 4 tokens -- still a failure below */
    }
    return (i == 4) ? 1 : 0;
}

/* Was updateGenie_Main() */
void app_genie_update_main(app_context_t *app)
{
    char date_str[12];

    /* Was `if(time_offset<0 || time_offset>0){ Settings.TimeZone +=
     * time_offset; SaveSettings(); SetDSTime((time_offset*3600) +
     * (Settings.Add30*1800)); }` -- rtc_apply_seconds_offset() already
     * implements SetDSTime()'s exact arithmetic (see rtc_time.h's own
     * doc comment: "Was ApplyTimeOffset()'s core arithmetic"), so this
     * composes two already-built primitives rather than needing new
     * timezone-math code. */
    if (app->time_offset != 0) {
        int64_t offset_seconds = (int64_t)app->time_offset * 3600
                                + (int64_t)app->settings.add30 * 1800;
        rtc_datetime_t new_time = rtc_apply_seconds_offset(&app->current_time, offset_seconds);

        app->settings.time_zone += app->time_offset;
        app_persist_settings(app);
        ds3231_rt1062_write(&new_time);
        app->current_time = new_time;
    }

#if APP_ENABLE_DISPLAY
    display_set_digits(GENIE_DLED_MIN, app->current_time.min);
    display_set_digits(GENIE_DLED_HOUR, app->current_time.hour);
    snprintf(date_str, sizeof(date_str), "%02d-%02d-20%02d",
             app->current_time.mday, app->current_time.mon, app->current_time.year % 100);
    display_set_string(GENIE_DATE_STR, date_str);

    display_set_digits(GENIE_DLED_ID2, app->settings.channel + 1);
    display_set_knob(GENIE_KNOB_PWR, app->settings.reader_power);
    display_set_digits(GENIE_DLED_POWER, app->settings.reader_power);

    /* UHF changes now strings on many objects -- was the block of
     * `genieWriteObject(GENIE_OBJ_STRINGS, ..., Settings.System)` calls,
     * a different Genie widget from a literal text write (see
     * display_set_string_index()'s own doc comment). */
    display_set_string_index(GENIE_SYSTEM_STR, app->settings.system);
    display_set_string_index(GENIE_LED1_STR, app->settings.system);
    display_set_string_index(GENIE_LED2_STR, app->settings.system);
    display_set_string_index(GENIE_LED3_STR, app->settings.system);
    display_set_string_index(GENIE_LED4_STR, app->settings.system);
    display_set_string_index(GENIE_BAT_OR_STATUS_STR, app->settings.system);
    if (app->settings.system) {
        display_set_string_index(GENIE_TXPDR_BAT_STR, app->uhf_reading);
    } else {
        display_set_led(DISPLAY_LED_LOW_POWER_MODE, app->rfid_cmd.chip_sleep);
    }
    display_set_string(GENIE_TXPDR_STR, app->last_lf_code);
#else
    (void)date_str;
#endif

    app->time_offset = 0;

    /* Was `if(board_vers>=32){ max_register = max_read(...REPSOC);
     * ... }` -- reads app->batt_percent (populated by
     * app_update_battery_percent(), app_loop.c) rather than re-deriving
     * a raw MAX17303 register read here. SIMPLIFIED 2026-07-22, per
     * explicit instruction ("the older boards <32 can be scrapped, this
     * processor will not be used on those older boards") -- the
     * board_version>=32 gate is now unconditional. */
#if APP_ENABLE_DISPLAY
    display_set_gauge(GENIE_GAUGE_BAT, app->batt_percent);
    display_set_digits(GENIE_DLED_BAT, app->batt_percent);
#endif
}

/* Was updateNetworkStrings() */
void app_genie_update_network_strings(app_context_t *app)
{
#if APP_ENABLE_DISPLAY
    char str[20];

    PRINTF("Network form: use_dhcp=%u rabbit_ip=%u.%u.%u.%u log.mounted=%d\r\n",
           (unsigned)app->settings.use_dhcp,
           app->settings.rabbit_ip[0], app->settings.rabbit_ip[1],
           app->settings.rabbit_ip[2], app->settings.rabbit_ip[3],
           app->log.mounted);

    if (app->settings.use_dhcp) {
#if APP_ENABLE_TCP
        uint8_t ip[4];
        enet_lwip_rt1062_get_ip(ip);
        format_dotted_quad(ip, str, sizeof(str));
#else
        format_dotted_quad(app->settings.rabbit_ip, str, sizeof(str));
#endif
    } else {
        format_dotted_quad(app->settings.rabbit_ip, str, sizeof(str));
    }
    display_set_string(GENIE_LAN_STR, str);

    format_dotted_quad(app->settings.gprs_server_ip1, str, sizeof(str));
    display_set_string(GENIE_REMOTE_STR, str);

    format_dotted_quad(app->settings.rabbit_gateway, str, sizeof(str));
    display_set_string(GENIE_GATEWAYIP_STR, str);

    snprintf(str, sizeof(str), "%u", (unsigned)app->settings.gprs_server_port);
    display_set_string(GENIE_PORT_STR, str);

    display_set_string(GENIE_APN_STR, app->settings.apn_name);

    snprintf(str, sizeof(str), "%02X:%02X:%02X",
             app->mac_address[3], app->mac_address[4], app->mac_address[5]);
    display_set_string(GENIE_MAC_STR, str);

    display_set_4dbutton(GENIE_DHCP, app->settings.use_dhcp);
    display_set_4dbutton(GENIE_REMOTE, app->settings.remote_type ? 1 : 0);
#else
    (void)app;
#endif
}

/*
 * Was UpdateRabbitIP()'s blocking Dynamic-C ifconfig() calls (bring
 * interface down, apply static IP or restart DHCP, bring back up).
 * Resolved 2026-07-16 -- wired to enet_lwip_rt1062_apply_network_settings(),
 * which faithfully ports both the DHCP-with-fallback and static-IP
 * branches (see that function's own header comment, including the
 * confirmed hardcoded 255.255.0.0 netmask and the DNS gap that's
 * deliberately not wired -- LWIP_DNS is off and nothing in this port
 * resolves hostnames yet).
 * GATED on APP_ENABLE_TCP: the underlying netif is only ever added to
 * lwIP (lwip_init()/netif_add()) by enet_lwip_rt1062_init(), itself
 * only called under that same flag in app_init.c -- calling any lwIP
 * netif function with that flag off would touch a netif lwIP never
 * added. Settings are still persisted correctly either way; they just
 * won't take effect on the live link until APP_ENABLE_TCP is on.
 */
static void app_genie_apply_network_settings(app_context_t *app)
{
#if APP_ENABLE_TCP
    /* Was previously unconditional under this #if -- FIXED 2026-07-20,
     * found via review while double-checking the ENET code (per
     * explicit request, given how often the debug probe needs
     * disconnecting to get a clean PHY link right now). APP_ENABLE_TCP
     * being on only means enet_lwip_rt1062_init() was CALLED -- it can
     * still fail at runtime (PHY preflight, exactly the failure mode
     * currently common with the probe attached), leaving
     * app->enet_available false and s_netif never added to lwIP at
     * all. Calling netif_set_down()/netif_set_addr()/dhcp_start() etc.
     * on a netif that was never netif_add()-ed is operating on
     * zeroed/garbage state -- a real crash risk, not just a silent
     * no-op, on exactly the kind of boot this project already hits
     * often. Settings are still persisted regardless (app_persist_settings()
     * runs at both call sites independently of this function) -- they
     * just won't take live effect until a boot where ENET actually
     * came up. */
    if (app->enet_available) {
        enet_lwip_rt1062_apply_network_settings(app->settings.use_dhcp,
                                                 app->settings.rabbit_ip,
                                                 app->settings.rabbit_gateway);

        /* FIXED 2026-07-21, per explicit report ("not reinitialising the
         * ethernet for DHCP when I turn DHCP on... it needs to show
         * up") -- app->dhcp_lease_printed gates app_loop.c's
         * trace_dhcp_lease_once(), which is what actually pushes the
         * Networking form's IP field update once a lease lands (see
         * that field's own doc comment in app_context.h). It's a
         * one-shot -- without resetting it here, only the very FIRST
         * DHCP negotiation ever, this whole boot, would refresh the
         * display; toggling DHCP off then back on again later would
         * genuinely get a new lease at the network level, but the
         * Networking form would never find out. Reset on every apply
         * (both directions, not just DHCP-on) so this is always freshly
         * armed for whatever the next DHCP negotiation turns out to be. */
        app->dhcp_lease_printed = 0;
    }
#else
    (void)app;
#endif
}

/*
 * TODO STUB: was check_fw()'s httpc-based HTTP GET + version-string
 * parse. This port already has the pure-logic pieces (fw_version_check.h)
 * and a generic downloader (fw_downloader.h) built for MCUboot, not
 * Rabbit's own updater -- gluing them to a real HTTP transport is a
 * separate task. Sets a clear "not implemented" status string rather
 * than silently doing nothing, and returns -1 (matches the original's
 * own "-1 = error" convention for new_firmware_avail).
 */
static int app_genie_check_fw_TODO(app_context_t *app)
{
#if APP_ENABLE_DISPLAY
    display_set_string(GENIE_FW_PROGRESS_STR, "Firmware check not implemented");
#endif
    (void)app;
    return -1;
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_FORM -- form-entered refresh                              */
/* ------------------------------------------------------------------ */

static void dispatch_form(app_context_t *app, int index)
{
    switch (index) {
    case GENIE_FORM_TIME:
#if APP_ENABLE_DISPLAY
        /* Was `genieWriteObject(GENIE_OBJ_SLIDER, GENIE_GPS, 1) //always
         * on for now` -- a SLIDER write reusing GENIE_GPS's 4Dbutton
         * enum ordinal (7) as the slider index, not the real
         * GENIE_4DBUTTON GPS write (that's the separate
         * display_set_4dbutton(GENIE_GPS, ...) call below, matching
         * `genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_GPS,
         * Settings.AutoSetGPSTime)`). Ported literally -- this looks
         * like an object-type mixup in the original itself, but per
         * this port's fidelity rule it's reproduced, not "corrected". */
        display_set_slider((genie_slider_t)GENIE_GPS, 1);
        {
            char sign[2] = { (app->settings.time_zone < 0) ? '-' : '+', '\0' };
            display_set_string(GENIE_SIGN_STR, sign);
        }
        display_set_trackbar(GENIE_TB_TIMEZ, app->settings.time_zone + 12);
        display_set_digits(GENIE_DLED_TIMEZONE, (app->settings.time_zone < 0 ? -app->settings.time_zone : app->settings.time_zone));
        display_set_4dbutton(GENIE_GPS, app->settings.auto_set_gps_time);
        display_set_4dbutton(GENIE_ADD30, app->settings.add30);
#endif
        app->last_winbutton = GENIE_BUTTON_BACK4;
        break;

    case GENIE_FORM_MAIN:
        app_genie_update_main(app);
        break;

    case GENIE_FORM_RFID:
        app->diag_visible = 0;
        break;

    case GENIE_FORM_ANT:
        app->diag_visible = 1;
        break;

    case GENIE_FORM_UHF:
        break;

    case GENIE_FORM_NETWORKING:
        app_genie_update_network_strings(app);
        break;

    case GENIE_FORM_DATA:
#if APP_ENABLE_DISPLAY
        {
            char str[24];
            int percent = 0;
            int y, mon, mday, hour, min, sec, wday;

            snprintf(str, sizeof(str), "%lu", (unsigned long)app->last_log_id);
            display_set_string(GENIE_RECORDS_STR, str);

            /* Was `mktm(&time_struc, Settings.iLastTimeSent); sprintf(
             * date_string3, "%02u:%02u:%02u   %02u-%02u-20%02u", ...)` --
             * composes already-built civil_epoch_to_ymdhms() rather than
             * needing a new time-formatting helper. */
            civil_epoch_to_ymdhms((int64_t)app->settings.last_time_sent,
                                   &y, &mon, &mday, &hour, &min, &sec, &wday);
            snprintf(str, sizeof(str), "%02d:%02d:%02d   %02d-%02d-20%02d",
                     hour, min, sec, mday, mon, y % 100);
            display_set_string(GENIE_LAST_READ_STR, str);

            (void)nand_log_check_percent_full(&app->log, &percent);
            display_set_gauge(GENIE_GAUGE_FILE, percent);
        }
#endif
        break;

    case GENIE_FORM_OTHER:
#if APP_ENABLE_DISPLAY
        display_set_4dbutton(GENIE_BUZZER, app->settings.beeper);
        display_set_slider(GENIE_SLIDER_DIM, app->settings.brightness);
        display_set_4dbutton(GENIE_DIM, app->settings.dim);
        display_set_4dbutton(GENIE_TRIGGER, 0);
        display_set_4dbutton(GENIE_SYSTEM, app->settings.system);
        {
            char ver[16];
            /* Was `sprintf(char_string, "V%d.%d", _FIRMWARE_VERSION_>>8,
             * _FIRMWARE_VERSION_&0xFF)` -- no confirmed
             * _FIRMWARE_VERSION_ constant exists in this port yet;
             * left as a placeholder string rather than inventing a
             * version number. */
            snprintf(ver, sizeof(ver), "V-.-");
            display_set_string(GENIE_INSTFW_STR, ver);
        }
        display_set_string(GENIE_FW_PROGRESS_STR, "");
#endif
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_USERBUTTON -- Sleep-form wake                             */
/* ------------------------------------------------------------------ */

static void dispatch_userbutton(app_context_t *app)
{
#if APP_ENABLE_DISPLAY
    display_set_contrast(app->settings.brightness);
    display_activate_form(GENIE_FORM_MAIN);
    display_set_string(GENIE_TXPDR_STR, app->last_lf_code);
#endif
    app->lo_backlight = 0;
    /* trigger_dim NOT ported -- process_display_events()'s existing
     * any-touch-event wake logic already un-dims unconditionally,
     * subsuming this button's original role of re-arming the dim timer
     * on wake. See app_context.h's field table for the full reasoning. */
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_KNOB -- reader power                                      */
/* ------------------------------------------------------------------ */

static void dispatch_knob(app_context_t *app, int data)
{
    app->settings.reader_power = (uint8_t)data;
#if APP_ENABLE_DISPLAY
    display_set_digits(GENIE_DLED_POWER, app->settings.reader_power);
    display_set_knob(GENIE_KNOB_PWR, app->settings.reader_power);
#endif
#if APP_ENABLE_NRF_SPI
    /* Was comms_NRF(0x03) */
    nrf_spi_set_reader_power(&app->nrf_transport, app->settings.reader_power);
#endif
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_TRACKBAR                                                   */
/* ------------------------------------------------------------------ */

static void dispatch_trackbar(app_context_t *app, int index, int data)
{
    switch (index) {
    case GENIE_TB_ID:
        app->settings.channel = (uint8_t)data;
#if APP_ENABLE_DISPLAY
        display_set_digits(GENIE_DLED_ID, app->settings.channel + 1);
        display_set_digits(GENIE_DLED_ID2, app->settings.channel + 1);
#endif
#if APP_ENABLE_NRF_SPI
        nrf_spi_set_channel(&app->nrf_transport, app->settings.channel); /* was comms_NRF(0x0A) */
#endif
        app_persist_settings(app);
        break;

    case GENIE_TB_ID2:
        app->settings.channel = (uint8_t)data;
#if APP_ENABLE_DISPLAY
        display_set_digits(GENIE_DLED_ID, app->settings.channel + 1);
        display_set_digits(GENIE_DLED_ID1, app->settings.channel + 1);
#endif
        app_persist_settings(app);
        break;

    case GENIE_TB_BT_ON:
        app->rfid_cmd.bt_adv = (uint8_t)data;
#if APP_ENABLE_DISPLAY
        if (data >= 0 && data < 8) {
            display_set_digits(GENIE_DLED_BT_TIME_ON, BT_ON_TIMES[data]);
            display_set_led(DISPLAY_LED_BT_ON, BT_ON_TIMES[data] ? 1 : 0);
        }
#endif
#if APP_ENABLE_NRF_SPI
        nrf_spi_set_bt_advertising(&app->nrf_transport, app->rfid_cmd.bt_adv); /* was comms_NRF(0x04) */
#endif
        break;

    case GENIE_TB_TIMEZ: {
        int dat = data - 12;
        app->time_offset = dat - app->settings.time_zone;
#if APP_ENABLE_DISPLAY
        {
            char sign[2] = { (dat < 0) ? '-' : '+', '\0' };
            display_set_string(GENIE_SIGN_STR, sign);
        }
        display_set_digits(GENIE_DLED_TIMEZONE, (uint16_t)(dat < 0 ? -dat : dat));
#endif
        break;
    }

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_4DBUTTON                                                   */
/* ------------------------------------------------------------------ */

static void dispatch_4dbutton(app_context_t *app, int index, int data)
{
    switch (index) {
    case GENIE_PLAYBACK:
        app->rfid_cmd.playback = (uint8_t)data;
#if APP_ENABLE_DISPLAY
        display_set_led(DISPLAY_LED_PLAYBACK, app->rfid_cmd.playback);
#endif
#if APP_ENABLE_NRF_SPI
        nrf_spi_set_playback_mode(&app->nrf_transport, app->rfid_cmd.playback); /* was comms_NRF(0x07) */
#endif
        break;

    case GENIE_SLEEP:
        app->rfid_cmd.chip_sleep = (uint8_t)data;
#if APP_ENABLE_DISPLAY
        display_set_led(DISPLAY_LED_LOW_POWER_MODE, app->rfid_cmd.chip_sleep);
#endif
#if APP_ENABLE_NRF_SPI
        nrf_spi_set_sleep_mode(&app->nrf_transport, app->rfid_cmd.chip_sleep); /* was comms_NRF(0x0B) */
#endif
        break;

    case GENIE_DHCP:
        app->settings.use_dhcp = data ? 1 : 0;
        /* Was ifdown(IF_ETH0)/while(ifpending(IF_ETH0)!=IF_DOWN){tcp_tick(NULL);}/
         * UpdateRabbitIP() -- the original's own explicit ifdown()+blocking-
         * wait-for-down step is subsumed by app_genie_apply_network_settings()
         * itself (its netif_set_down() call is unconditional, covering both
         * branches, and lwIP's netif_set_down() is synchronous -- no
         * separate wait needed, consistent with this port's non-blocking
         * architecture rule). See app_genie_apply_network_settings(). */
        app_genie_apply_network_settings(app);
        app_persist_settings(app);
        /* DELIBERATE DEVIATION from source, 2026-07-21, per explicit
         * instruction -- the original's own GENIE_DHCP handler
         * (ACTIVERFID_V1.02_UHF.c:1725-1738) does NOT refresh the
         * Networking form's fields here; GENIE_LAN_STR only ever
         * updates when the form is re-activated (updateNetworkStrings()
         * at the GENIE_FORM_NETWORKING event) or, for DHCP specifically,
         * asynchronously once a lease completes. That's faithful but
         * clunky UX -- toggling the switch left the displayed IP stale
         * until you left and re-entered the screen. Refreshing
         * immediately here instead. Note this doesn't change what the
         * DHCP branch can actually show: DHCP negotiation is
         * asynchronous, so switching TO DHCP will still show whatever
         * enet_lwip_rt1062_get_ip() currently holds (likely 0.0.0.0 or
         * stale) until a real lease lands later -- this refresh can't
         * make an IP appear before DHCP has actually gotten one, it
         * just stops the STATIC-mode case from looking stale. */
        app_genie_update_network_strings(app);
        break;

    case GENIE_REMOTE:
        /* Was `max_write(MAX17303_ADDRESS1, MAX17303_FAULTS, 0x00)` --
         * previously stubbed here since no confirmed register constant
         * existed in this port; max17303_fuel_gauge_rt1062.h now
         * defines MAX17303_REG_FAULTS (0xAF, MAIN address), confirmed
         * from this exact call site -- see that header's comment.
         * Wired 2026-07-16. Compiled in only when APP_ENABLE_BMS is on --
         * NOT a hard technical dependency (max17303_write_reg() itself
         * only needs the shared I2C bus, which lpi2c1_bus_rt1062_init()
         * sets up unconditionally regardless of this flag), but this
         * port treats every MAX17303/MP2731 register WRITE as
         * safety-relevant and keeps them all behind the same
         * explicit-consent flag as bms_init()'s own setpoint writes,
         * rather than letting one MAX17303 write reach real hardware
         * through a code path independent of that decision. */
#if APP_ENABLE_BMS
        max17303_write_reg(MAX17303_ADDR_MAIN, MAX17303_REG_FAULTS, 0x00u);
#endif
        if (data) {
            if (!app->settings.remote_type) {
                app->settings.remote_type = 2;
            }
        } else {
            app->settings.remote_type = 0;
            /* Was Toggle_Modem() -- its real body was previously
             * unavailable (flagged, not guessed); user pasted it
             * 2026-07-16, confirming gprs_modem_toggle() (built in an
             * earlier session, before this exact source was available
             * to check against) already matches it faithfully:
             * set_wake_pin/set_power_enable/open/flush/AT-command
             * sequence line up in the same order for both the
             * remote-enabled and remote-disabled branches. Called here
             * with remote_enabled=0 since Settings.RemoteType was just
             * set to 0 above -- matches the original calling Toggle_Modem()
             * with no args right after the same assignment (it reads
             * Settings.RemoteType internally). Two things in the pasted
             * source were deliberately NOT ported: `genieWriteObject(
             * GENIE_OBJ_TANK, GENIE_TANK_4G, 0)` (a display call, kept
             * out of gprs_modem.c's transport-only orchestration layer
             * by design -- added directly below instead) and a trailing
             * `printf(buf,"\n\r")` that passes buf (still holding the
             * last AT command string) as a format string -- reads as a
             * leftover/buggy debug statement in the original, not real
             * device I/O, so not carried over.
             * GATED on APP_ENABLE_GPRS: gprs_modem_toggle() dereferences
             * app->modem.transport, only ever set by gprs_modem_init()
             * in app_init.c's own APP_ENABLE_GPRS block -- calling this
             * with that flag off would be a NULL transport dereference. */
#if APP_ENABLE_GPRS
            gprs_modem_toggle(&app->modem, 0);
#endif
#if APP_ENABLE_DISPLAY
            display_set_tank(GENIE_TANK_4G, 0);
#endif
        }
        app_persist_settings(app);
        break;

    case GENIE_BUZZER:
        app->settings.beeper = data ? 1 : 0;
        app_persist_settings(app);
        break;

    case GENIE_DIM:
        app->settings.dim = data ? 1 : 0;
        app_persist_settings(app);
        break;

    case GENIE_TRIGGER:
        app->settings.trigger_on = data ? 1 : 0;
#if APP_ENABLE_DISPLAY
        if (data) {
            /* Was `genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_TRIGGER, 0)`
             * -- "disabling trigger as there is no circuit now for it
             * in UHF version", ported verbatim: turning it on visually
             * turns it right back off. */
            display_set_4dbutton(GENIE_TRIGGER, 0);
        }
#endif
        /* Interrupt-enable side (`WrPortI(I1CR,...)`) is already a
         * flagged-unconfirmed open item -- see CLAUDE.md's TRIGGER pin
         * note -- not duplicated here. */
        app_persist_settings(app);
        break;

    case GENIE_ADD30:
        app->settings.add30 = data ? 1 : 0;
        app_persist_settings(app);
        break;

    case GENIE_GPS:
        app->settings.auto_set_gps_time = data ? 1 : 0;
        app_persist_settings(app);
        break;

    case GENIE_RFID:
#if APP_ENABLE_DISPLAY
        if (app->settings.system) {
            display_activate_form(GENIE_FORM_UHF);
            display_set_digits(GENIE_DLED_ID1, app->settings.channel + 1);
            display_set_trackbar(GENIE_TB_ID2, app->settings.channel);
            display_set_4dbutton(GENIE_UHF_READ, app->uhf_reading);
            display_set_4dbutton(GENIE_UHF_MODE, app->uhf_mode);
            display_set_string_index(GENIE_COUNTRY_STR, app->settings.uhf_region);
            display_set_string_index(GENIE_FREQ_STR, app->settings.uhf_region);
        } else {
            display_activate_form(GENIE_FORM_RFID);
            display_set_digits(GENIE_DLED_POWER_CHANGE, app->settings.reader_power);
            display_set_knob(GENIE_KNOB_PWR, app->settings.reader_power);
            display_set_digits(GENIE_DLED_ID, app->settings.channel + 1);
            display_set_trackbar(GENIE_TB_ID, app->settings.channel);
            if (app->rfid_cmd.bt_adv < 8) {
                display_set_digits(GENIE_DLED_BT_TIME_ON, BT_ON_TIMES[app->rfid_cmd.bt_adv]);
            }
            display_set_trackbar(GENIE_TB_BT_ON, app->rfid_cmd.bt_adv);
            display_set_4dbutton(GENIE_PLAYBACK, app->rfid_cmd.playback);
            display_set_4dbutton(GENIE_SLEEP, app->rfid_cmd.chip_sleep);
        }
#endif
        break;

    case GENIE_SYSTEM:
        /* Already fully ported -- was UHF/Active toggle, see
         * app_uhf_active_mode_toggle()'s own doc comment for the exact
         * original source this matches. */
        app_uhf_active_mode_toggle(app, data);
        app->settings.system = data ? 1 : 0;
        app_persist_settings(app);
        break;

    case GENIE_UHF_READ:
        app_uhf_reader_control(app, data);
        break;

    case GENIE_UHF_MODE:
        app->uhf_mode = data;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_SLIDER -- brightness                                      */
/* ------------------------------------------------------------------ */

static void dispatch_slider(app_context_t *app, int data)
{
    if (data < 1) {
        data = 1;
#if APP_ENABLE_DISPLAY
        display_set_slider(GENIE_SLIDER_DIM, data);
#endif
    }
    app->settings.brightness = (uint8_t)data;
    app_persist_settings(app);
#if APP_ENABLE_DISPLAY
    display_set_contrast(app->settings.brightness);
#endif
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_WINBUTTON                                                  */
/* ------------------------------------------------------------------ */

static void dispatch_winbutton(app_context_t *app, int index)
{
    switch (index) {
    case GENIE_BUTTON_CODE:
        app->last_winbutton = GENIE_BUTTON_CODE;
        if (app->admin) {
#if APP_ENABLE_NRF_SPI
            nrf_spi_transmitter_off(&app->nrf_transport); /* was comms_NRF(0x0C) */
#endif
            strncpy(app->keyboard_string, app->last_lf_code, sizeof(app->keyboard_string) - 1);
            app->keyboard_string[sizeof(app->keyboard_string) - 1] = '\0';
#if APP_ENABLE_DISPLAY
            display_activate_form(GENIE_FORM_KEYBOARD);
#endif
            /* Was the "bump the last digit" auto-increment for the next
             * factory-programmed code. */
            {
                size_t len = strlen(app->keyboard_string);
                if (len >= 6) {
                    if (app->keyboard_string[len - 1] == '9') {
                        app->keyboard_string[len - 2] += 1;
                        app->keyboard_string[len - 1] = '0';
                    } else {
                        app->keyboard_string[len - 1] += 1;
                    }
                }
            }
#if APP_ENABLE_DISPLAY
            display_set_string(GENIE_KB_STR, app->keyboard_string);
#endif
        } else {
#if APP_ENABLE_DISPLAY
            display_activate_form(GENIE_FORM_KEYPAD);
            display_set_string(GENIE_KP_STR, "Enter admin pin");
#endif
            app->keyboard_string[0] = '\0';
            app->enter_pin = 1;
        }
        break;

    case GENIE_BUTTON_UHF_CHANGE:
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_KEYPAD);
#endif
        if (app->admin) {
            snprintf(app->keyboard_string, sizeof(app->keyboard_string), "%d", app->settings.uhf_region + 1);
#if APP_ENABLE_DISPLAY
            display_set_string(GENIE_KP_STR, app->keyboard_string);
#endif
        } else {
#if APP_ENABLE_DISPLAY
            display_set_string(GENIE_KP_STR, "Enter admin pin");
#endif
            app->keyboard_string[0] = '\0';
            app->enter_pin = 1;
        }
        app->last_winbutton = GENIE_BUTTON_UHF_CHANGE;
        break;

    case GENIE_BUTTON_RESET:
#if APP_ENABLE_DISPLAY
        display_set_digits(GENIE_DLED_READS, 0);
#endif
        app->chip_reads = 0;
        break;

    case GENIE_BUTTON_SETREMOTE_IP:
        app->last_winbutton = GENIE_BUTTON_SETREMOTE_IP;
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_KEYPAD);
#endif
        format_dotted_quad(app->settings.gprs_server_ip1, app->keyboard_string, sizeof(app->keyboard_string));
#if APP_ENABLE_DISPLAY
        display_set_string(GENIE_KP_STR, app->keyboard_string);
#endif
        break;

    case GENIE_BUTTON_SETGATEWAY_IP:
        app->last_winbutton = GENIE_BUTTON_SETGATEWAY_IP;
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_KEYPAD);
#endif
        format_dotted_quad(app->settings.rabbit_gateway, app->keyboard_string, sizeof(app->keyboard_string));
#if APP_ENABLE_DISPLAY
        display_set_string(GENIE_KP_STR, app->keyboard_string);
#endif
        break;

    case GENIE_BUTTON_SETREMOTE_PORT:
        app->last_winbutton = GENIE_BUTTON_SETREMOTE_PORT;
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_KEYPAD);
#endif
        snprintf(app->keyboard_string, sizeof(app->keyboard_string), "%u", (unsigned)app->settings.gprs_server_port);
#if APP_ENABLE_DISPLAY
        display_set_string(GENIE_KP_STR, app->keyboard_string);
#endif
        break;

    case GENIE_BUTTON_SETAPN:
        app->last_winbutton = GENIE_BUTTON_SETAPN;
        strncpy(app->keyboard_string, app->settings.apn_name, sizeof(app->keyboard_string) - 1);
        app->keyboard_string[sizeof(app->keyboard_string) - 1] = '\0';
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_KEYBOARD);
        display_set_string(GENIE_KB_STR, app->keyboard_string);
#endif
        break;

    case GENIE_BUTTON_SETLAN:
        app->last_winbutton = GENIE_BUTTON_SETLAN;
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_KEYPAD);
#endif
        format_dotted_quad(app->settings.rabbit_ip, app->keyboard_string, sizeof(app->keyboard_string));
#if APP_ENABLE_DISPLAY
        display_set_string(GENIE_KP_STR, app->keyboard_string);
#endif
        break;

    case GENIE_BUTTON_CANCEL:
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_MAIN);
#endif
        app_genie_update_main(app);
        break;

    case GENIE_BUTTON_CLEAR_LOG:
        app->last_winbutton = GENIE_BUTTON_CLEAR_LOG;
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_KEYBOARD);
        display_set_string(GENIE_KB_STR, "WARNING 'Y' to delete");
#endif
        break;

    case GENIE_BUTTON_CHECK:
        app->new_firmware_avail = app_genie_check_fw_TODO(app);
        break;

    case GENIE_BUTTON_UPDATE_FIRMWARE:
#if APP_ENABLE_NRF_SPI
        nrf_spi_enter_dfu(&app->nrf_transport); /* was comms_NRF(0xFA) */
#endif
        /* TODO STUB -- was `if(new_firmware_avail) install_firmware();`.
         * See this file's header comment. */
        break;

    case GENIE_BUTTON_CHECK_RL:
        if (!app->uhf_reading) {
#if APP_ENABLE_DISPLAY
            int j;
            for (j = 1; j < 5; j++) {
                display_set_gauge((genie_gauge_t)(j + 2), 0); /* clear ant1-4 gauges */
            }
#endif
            app->diag_visible = 1;
#if APP_ENABLE_UHF
            uhf_reader_open(&app->uhf, &app->uhf_transport);
            uhf_reader_initialise(&app->uhf, (uhf_region_t)app->settings.uhf_region,
                                   app->settings.channel, app->uhf_mode);
#endif
        }
        break;

    case GENIE_BUTTON_DIAGNOSTICS:
        app->diag_visible = 1;
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* GENIE_OBJ_KEYBOARD / GENIE_KEYPAD -- text entry state machine       */
/* ------------------------------------------------------------------ */

static void dispatch_keyboard(app_context_t *app, int data)
{
    if (app->last_winbutton == GENIE_BUTTON_CLEAR_LOG) {
        if (data == 0x59 || data == 0x79) { /* Y or y */
            nand_log_reset(&app->log);
        }
        /* Was `genieWriteObject(GENIE_OBJ_STRINGS, GENIE_RECORDS_STR/
         * LAST_READ_STR, ...)` here -- the ORIGINAL's own inconsistency
         * vs FORM_DATA's refresh above, which uses genieWriteStr
         * (literal text) for these same two fields. Ported literally,
         * not "fixed", per this port's fidelity rule. */
#if APP_ENABLE_DISPLAY
        display_set_string_index(GENIE_RECORDS_STR, (int)app->last_log_id);
        display_set_string_index(GENIE_LAST_READ_STR, 0);
        display_set_gauge(GENIE_GAUGE_FILE, 0);
        display_activate_form(GENIE_FORM_DATA);
#endif
        return;
    }

    if (data == '\r') {
        if (app->last_winbutton == GENIE_BUTTON_SETAPN) {
            strncpy(app->settings.apn_name, app->keyboard_string, sizeof(app->settings.apn_name) - 1);
            app->settings.apn_name[sizeof(app->settings.apn_name) - 1] = '\0';
            app_persist_settings(app);
#if APP_ENABLE_DISPLAY
            display_activate_form(GENIE_FORM_NETWORKING);
#endif
            app_genie_update_network_strings(app);
        } else if (app->last_winbutton == GENIE_BUTTON_CODE) {
            /* Was `strncpy(txpdr_string, keyboard_string, 6);
             * memcpy(RFID.chip_code, txpdr_string, 6);` -- the typed
             * string IS the raw 6-byte code (not parsed as decimal),
             * ported literally per this port's fidelity rule. */
            strncpy(app->last_lf_code, app->keyboard_string, 6);
            app->last_lf_code[6] = '\0';
            memcpy(app->rfid_cmd.chip_code, app->keyboard_string, 6);
#if APP_ENABLE_DISPLAY
            display_activate_form(GENIE_FORM_DATA);
            display_set_string(GENIE_CODE_STR, app->last_lf_code);
#endif
            app->encode_tag = 1;
        }
    } else if (data == 0x08) { /* backspace */
        size_t len = strlen(app->keyboard_string);
        if (len > 0) {
            app->keyboard_string[len - 1] = '\0';
#if APP_ENABLE_DISPLAY
            display_set_string(GENIE_KB_STR, app->keyboard_string);
#endif
        }
    } else {
        size_t len = strlen(app->keyboard_string);
        if (len < 29 && len + 1 < sizeof(app->keyboard_string)) {
            app->keyboard_string[len] = (char)data;
            app->keyboard_string[len + 1] = '\0';
#if APP_ENABLE_DISPLAY
            display_set_string(GENIE_KB_STR, app->keyboard_string);
#endif
        }
    }
}

static void dispatch_keypad(app_context_t *app, int data)
{
    if (data == 11) { /* back */
        size_t len = strlen(app->keyboard_string);
        if (len > 0) {
            app->keyboard_string[len - 1] = '\0';
#if APP_ENABLE_DISPLAY
            display_set_string(GENIE_KP_STR, app->keyboard_string);
#endif
        }
        return;
    }

    if (data == 12) { /* enter */
        if (app->last_winbutton == GENIE_BUTTON_SETLAN) {
            uint8_t ip[4];
            if (app_genie_parse_ip(app->keyboard_string, ip)) {
                memcpy(app->settings.rabbit_ip, ip, 4);
#if APP_ENABLE_DISPLAY
                display_set_4dbutton(GENIE_DHCP, 0);
#endif
                app->settings.use_dhcp = 0;
                /* Was parseIP_check()'s ip_type==1 branch: `if(!Settings.
                 * useDHCP){ ifdown/wait/UpdateRabbitIP(); }` -- gated in
                 * the original on DHCP already being off at entry time
                 * (typing a static IP while DHCP is active just stored
                 * the value there, no live effect). This port's own
                 * GENIE_BUTTON_SETLAN handling (pre-existing, not
                 * introduced here) unconditionally forces use_dhcp=0
                 * a few lines up, so by this point that original gate
                 * is always satisfied -- call unconditionally to match. */
                app_genie_apply_network_settings(app);
            }
        } else if (app->last_winbutton == GENIE_BUTTON_SETREMOTE_IP) {
            uint8_t ip[4];
            if (app_genie_parse_ip(app->keyboard_string, ip)) {
                memcpy(app->settings.gprs_server_ip1, ip, 4);
            }
        } else if (app->last_winbutton == GENIE_BUTTON_SETREMOTE_PORT) {
            app->settings.gprs_server_port = (uint16_t)atoi(app->keyboard_string);
        } else if (app->last_winbutton == GENIE_BUTTON_SETGATEWAY_IP) {
            uint8_t ip[4];
            if (app_genie_parse_ip(app->keyboard_string, ip)) {
                memcpy(app->settings.rabbit_gateway, ip, 4);
            }
        } else if (app->enter_pin) {
            if (atoi(app->keyboard_string) == APP_GENIE_ADMIN_PIN) {
                app->admin = 1;
            }
            app->enter_pin = 0;
#if APP_ENABLE_DISPLAY
            if (app->last_winbutton == GENIE_BUTTON_CODE) {
                display_activate_form(GENIE_FORM_DATA);
            } else if (app->last_winbutton == GENIE_BUTTON_UHF_CHANGE) {
                display_activate_form(GENIE_FORM_UHF);
            }
#endif
            return;
        }

        if (app->last_winbutton == GENIE_BUTTON_UHF_CHANGE) {
            int region = atoi(app->keyboard_string);
            if (region < 7) {
                app->settings.uhf_region = (uint8_t)(region - 1);
                app_persist_settings(app);
            }
#if APP_ENABLE_DISPLAY
            display_activate_form(GENIE_FORM_UHF);
            display_set_string_index(GENIE_COUNTRY_STR, app->settings.uhf_region);
            display_set_string_index(GENIE_FREQ_STR, app->settings.uhf_region);
#endif
            return;
        }

        if (app->last_winbutton == GENIE_BUTTON_CODE) {
#if APP_ENABLE_DISPLAY
            display_activate_form(GENIE_FORM_DATA);
#endif
            return;
        }

#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_NETWORKING);
#endif
        app_genie_update_network_strings(app);
        app_persist_settings(app);
        return;
    }

    /* a number or '.' */
    {
        size_t len = strlen(app->keyboard_string);
        if (len < 21 && len + 1 < sizeof(app->keyboard_string)) {
            app->keyboard_string[len] = (char)data;
            app->keyboard_string[len + 1] = '\0';
#if APP_ENABLE_DISPLAY
            display_set_string(GENIE_KP_STR, app->keyboard_string);
#endif
        }
    }
}

/* ------------------------------------------------------------------ */
/* Top-level dispatch                                                   */
/* ------------------------------------------------------------------ */

void app_dispatch_genie_event(app_context_t *app, const display_event_t *ev)
{
    switch (ev->object) {
    case GENIE_OBJ_FORM:
        dispatch_form(app, ev->index);
        break;
    case GENIE_OBJ_USERBUTTON:
        dispatch_userbutton(app);
        break;
    case GENIE_OBJ_KNOB:
        dispatch_knob(app, ev->data);
        break;
    case GENIE_OBJ_TRACKBAR:
        dispatch_trackbar(app, ev->index, ev->data);
        break;
    case GENIE_OBJ_4DBUTTON:
        dispatch_4dbutton(app, ev->index, ev->data);
        break;
    case GENIE_OBJ_SLIDER:
        dispatch_slider(app, ev->data);
        break;
    case GENIE_OBJ_WINBUTTON:
        dispatch_winbutton(app, ev->index);
        break;
    case GENIE_OBJ_KEYBOARD:
        if (ev->index == GENIE_KEYBOARD) {
            dispatch_keyboard(app, ev->data);
        } else if (ev->index == GENIE_KEYPAD) {
            dispatch_keypad(app, ev->data);
        }
        break;
    default:
        /* Was the GENIE_REPORT_OBJ / GENIE_PING branches -- the
         * original's own bodies are nearly empty there too (commented-
         * out example code / no-op branches for DISCONNECTED/READY/
         * ACK/NAK); display_dequeue_event() already filters out
         * cmd==GENIE_PING entirely (see display_stub.c), and no
         * GENIE_OBJ_USER_LED call site exists in this port, so there's
         * genuinely nothing to do here -- matches what's actually in
         * the original, not silently dropped. */
        break;
    }
}
