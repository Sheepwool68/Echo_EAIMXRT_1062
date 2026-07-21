#include "app_pc_dispatch.h"
#include "app_genie_dispatch.h"
#include "display_stub.h"
#include "civil_time.h"
#include "bringup_config.h"
#include "settings_store.h"
#include "reader_shutdown_rt1062.h"
#include "fan_rt1062.h"
#include "button_led_rt1062.h"
#include "buzzer_rt1062.h"
#include "debug_console_rt1062.h"
#include <string.h>
#include <stdio.h>

/* PRINTF redirect to LPUART5 -- see debug_console_rt1062.h. */
#undef PRINTF
/* SILENCED 2026-07-21, per explicit request ("printf on ethernet
 * comms only after boot"). Was `debug_printf`. Restore if this
 * tracing is wanted again. */
#define PRINTF(...) ((void)0)

/* Was SaveSettings() -- see settings_store.h. Gated on app->log.mounted
 * so this is a safe no-op with APP_ENABLE_STORAGE off, same "everything
 * still runs, just inert" philosophy as the other bring-up flags --
 * settings changes still take effect in RAM for the current session,
 * they just won't survive a reset until storage is enabled. */
static void app_persist_settings(app_context_t *app)
{
    if (app->log.mounted) {
        settings_store_save(&app->log.lfs, &app->settings);
    }
}

static void send_ip_setting(const tcp_socket_transport_t *t, uint8_t setting_id, const uint8_t ip[4])
{
    char buf[64], ipstr[20];
    int n;
    snprintf(ipstr, sizeof(ipstr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    n = pc_format_setting_str(setting_id, ipstr, buf, sizeof(buf));
    if (n > 0) {
        t->send(t->ctx, (const uint8_t *)buf, (size_t)n);
    }
}

static void send_int_setting(const tcp_socket_transport_t *t, uint8_t setting_id, int value)
{
    char buf[32];
    int n = pc_format_setting_int(setting_id, value, buf, sizeof(buf));
    if (n > 0) {
        t->send(t->ctx, (const uint8_t *)buf, (size_t)n);
    }
}

static void send_long_setting(const tcp_socket_transport_t *t, uint8_t setting_id, unsigned long value)
{
    char buf[32];
    int n = pc_format_setting_long(setting_id, value, buf, sizeof(buf));
    if (n > 0) {
        t->send(t->ctx, (const uint8_t *)buf, (size_t)n);
    }
}

static void send_str_setting(const tcp_socket_transport_t *t, uint8_t setting_id, const char *value)
{
    char buf[64];
    int n = pc_format_setting_str(setting_id, value, buf, sizeof(buf));
    if (n > 0) {
        t->send(t->ctx, (const uint8_t *)buf, (size_t)n);
    }
}

/* Was SendSettings() */
static void app_send_settings_dump(app_context_t *app, const tcp_socket_transport_t *t)
{
    const device_settings_t *s = &app->settings;

    send_int_setting(t, 0x01, s->remote_type);
    send_ip_setting(t, 0x02, s->gprs_server_ip1);
    send_int_setting(t, 0x03, s->gprs_server_port);
    send_str_setting(t, 0x04, s->apn_name);
    send_str_setting(t, 0x05, s->apn_user);
    send_str_setting(t, 0x06, s->apn_password);
    send_int_setting(t, 0x09, s->output_type);
    send_long_setting(t, 0x1E, 3);
    send_int_setting(t, 0x1F, s->channel);
    send_int_setting(t, 0x20, 0);
    send_int_setting(t, 0x21, s->beeper);
    send_int_setting(t, 0x22, s->auto_set_gps_time);
    send_int_setting(t, 0x23, s->time_zone);
    send_int_setting(t, 0x24, s->data_on_request);
    send_int_setting(t, 0x25, s->channel);
    send_ip_setting(t, 0x2A, s->rabbit_gateway);
    send_ip_setting(t, 0x2B, s->rabbit_dns);
    send_ip_setting(t, 0x2C, s->rabbit_ip);
    send_int_setting(t, 0x2E, s->send_data_to_remote_server);
    send_ip_setting(t, 0x30, s->gprs_server_ip2);
    send_ip_setting(t, 0x31, s->gprs_server_ip3);
}

/*
 * Was the GENIE_SYSTEM branch of the touch-event handler (a distinct
 * piece from UHF_Reader_Control()/app_uhf_reader_control() above --
 * this is a lower-level "fully power the reader hardware up/down and
 * switch the nRF52833's mode" toggle, not the higher-level "start/stop
 * the scanning loop" operation.
 *
 * FIDELITY FIX, 2026-07-18: this doc comment itself was stale/incomplete
 * (transcribed with two commands missing) -- re-confirmed directly
 * against ACTIVERFID_V1.02_UHF.c lines 1797-1808, the real source is:
 *
 *   if(indata==0){  //switched off UHF from Other Toggle
 *      serEclose;
 *      BitWrPortI(PBDR, &PBDRShadow, 1, 4);    //turn off reader
 *      comms_NRF(0x0A);    //set the schannel
 *      comms_NRF(0x03);    //set the LF power on the nrf52833
 *   }else{
 *      BitWrPortI(PBDR, &PBDRShadow, 0, 4);    //turn on reader
 *      comms_NRF(0x0C);    // turn of LF transmitter
 *      Open_Reader();
 *   }
 *
 * Two calls were missing from the port to match: the off-branch's
 * comms_NRF(0x0A) (channel push) BEFORE 0x03, and the on-branch's
 * comms_NRF(0x0C) (transmitter off) BEFORE Open_Reader(). Both added
 * below. uhf_enabled matches the original's `indata` truthiness (0 =
 * off, nonzero = on).
 */
void app_uhf_active_mode_toggle(app_context_t *app, int uhf_enabled)
{
    (void)app; /* unused if both APP_ENABLE_UHF and APP_ENABLE_NRF_SPI are off */

    if (!uhf_enabled) {
#if APP_ENABLE_UHF
        app->uhf_transport.close(app->uhf_transport.ctx);
#endif
        reader_power_set(0); /* SHUTDOWN high + PWR low -- both pins together */
#if APP_ENABLE_NRF_SPI
        /* Was comms_NRF(0x0A) -- push the configured channel, same
         * command already sent elsewhere (see app_init.c's boot-time
         * push, and app_genie_dispatch.c's ID trackbar handler). Fixed
         * 2026-07-18 -- was missing here, see this function's own doc
         * comment above. */
        nrf_spi_set_channel(&app->nrf_transport, app->settings.channel);
        /* Was comms_NRF(0x03) -- same command already mapped
         * elsewhere in this port (see app_init.c's Active/LF-mode
         * branch) to pushing the configured reader power, not a
         * literal power_percent=0x03 argument. */
        nrf_spi_set_reader_power(&app->nrf_transport, app->settings.reader_power);
#endif
    } else {
        reader_power_set(1); /* SHUTDOWN low + PWR high -- both pins together */
#if APP_ENABLE_NRF_SPI
        /* Was comms_NRF(0x0C) -- turn off the LF transmitter before the
         * reader opens. Fixed 2026-07-18 -- was missing here, see this
         * function's own doc comment above. */
        nrf_spi_transmitter_off(&app->nrf_transport);
#endif
#if APP_ENABLE_UHF
        uhf_reader_open(&app->uhf, &app->uhf_transport);
#endif
    }
}

/* Was UHF_Reader_Control() */
void app_uhf_reader_control(app_context_t *app, int enable)
{
    /* TEMPORARY DIAGNOSTIC, added 2026-07-17 per explicit report
     * ("break of reading loop", not a board freeze -- 10s or so into a
     * previously-working read session, all UHF activity goes silent
     * with the rest of the board still alive). uhf_reading can ONLY
     * ever become 0 from inside this function (the antenna-lost check
     * just below, or the disable branch) -- nothing in the per-
     * iteration main loop touches it. This traces every call so a
     * silent stop shows up here with a real cause, distinguishing "the
     * app itself decided to stop" from "uhf_reading is still 1 but the
     * transport/reader stopped producing anything" (which would show
     * NO print here at all when the stall happens). Remove once the
     * cause is confirmed. */
    PRINTF("UHF control: enable=%d (was reading=%d)\r\n", enable, app->uhf_reading);

    if (enable) {
        app->program_state = APP_STATE_READING;

        /* Button-press acknowledgment beep -- ONE pulse, fires
         * immediately, before Open_Reader()/TM_InitialiseReader() even
         * run. REVISED 2026-07-17, per explicit instruction: previously
         * this was the full two-pulse "starting reading" beep moved
         * here for instant feedback; now split into two separate
         * signals -- an immediate single beep confirming the button
         * press was received, and a SEPARATE double beep (below, in the
         * antennas-confirmed branch) confirming the reader has actually
         * started reading after initialisation completes. Neither is
         * gated on Settings.Beeper (matches the original's Beep() call
         * sites having no such gate of their own -- see
         * buzzer_beep_n_blocking()'s doc comment) and both block
         * (matches the original's raw BitWrPortI()/msDelay() toggle,
         * not an async queued pulse train). Position (button press,
         * before Open_Reader()) is still a deliberate, explicitly-
         * instructed deviation from the original's exact placement (see
         * git history/project memory for that first instruction) --
         * this single-beep-on-press/double-beep-on-confirmed-start split
         * is a further explicit refinement of that same deviation, not
         * a fidelity guess. */
        buzzer_beep_n_blocking(1);

#if APP_ENABLE_UHF
        /* Was Open_Reader() + TM_InitialiseReader() -- the original
         * reopened the UART and re-ran the full init sequence every
         * time reading started, rather than keeping the link open
         * continuously; matched here for fidelity. */
        uhf_reader_open(&app->uhf, &app->uhf_transport);
        uhf_reader_initialise(&app->uhf, (uhf_region_t)app->settings.uhf_region,
                               app->settings.channel, app->uhf_mode);
#endif

        /* Was never a concern in the original (fresh Open_Reader() per
         * session there too) -- added 2026-07-17 alongside
         * process_uhf_reading()'s new cross-read reassembly buffer
         * (app_loop.c). uhf_reader_open() above already flushes the
         * transport's own RX ring via t->flush_rx()/t->flush_tx(), but
         * that doesn't touch app->uhf_carry_buf, which lives outside
         * the transport. Any bytes left over from a PREVIOUS reading
         * session are stale once the link has been closed and reopened
         * -- carrying them into a fresh session's first read would
         * misalign the parser against genuinely new data. */
        app->uhf_carry_len = 0;

        app->uhf_reading = 1;

        {
            int percent = -1;
            if (nand_log_check_percent_full(&app->log, &percent) == 0
                && nand_log_should_auto_reset(percent)) {
                nand_log_reset(&app->log);
            }
        }

#if APP_ENABLE_UHF
        uhf_reader_start(&app->uhf, 0 /* heartbeat_enabled -- TODO confirm desired default */);
#endif

        if (app->uhf.ants == 0) {
            app->program_state = APP_STATE_IDLE;
            app->uhf_reading = 0;
        } else {
            /* Was the FAN_CONTROL pin write inside StartReaders()'s
             * `if(ants)` branch (UHF_READER.LIB line 904), immediately
             * after the start command is actually sent -- CONFIRMED
             * FIXED 2026-07-17, an earlier version of this port called
             * fan_on() unconditionally at the very top of this
             * function's enable branch, which would turn the fan on
             * even when no antenna is connected and reading never
             * actually starts (StartReaders()'s `else` branch aborts
             * with no fan-on at all). This `else` here is exactly
             * that same antennas-present condition, so placing it here
             * matches. */
            fan_on();

            /* "Reader actually started reading" confirmation beep --
             * TWO pulses, added 2026-07-17 per explicit instruction (see
             * the button-press beep's own comment above for the full
             * split). Placed here specifically because this is the
             * ONLY branch that means the reader genuinely began
             * inventory -- antennas confirmed present AND
             * uhf_reader_start() actually sent the start command (see
             * uhf_reader_start()'s own `if (r->ants == 0) return 0;`
             * guard). The no-antenna branch deliberately does NOT get
             * this beep -- reading never actually started there, so a
             * "started reading" confirmation would be misleading. */
            buzzer_beep_n_blocking(2);

            app->settings.shutdown_status = 1; /* ABNORMAL_SHUTDOWN --
                persisted so an unclean power-loss auto-resumes reading
                on next boot (see app_init.c) */
            app_persist_settings(app);
        }

        /* Was `genieActivateForm(GENIE_FORM_MAIN); updateGenie_Main();`
         * -- unconditional tail of UHF_Reader_Control()'s start branch,
         * runs regardless of whether the ants check above just reset
         * uhf_reading back to 0. app_genie_update_main() itself writes
         * GENIE_TXPDR_BAT_STR from app->uhf_reading (when Settings.System
         * is set), so it naturally reflects the final start/no-antenna
         * outcome without a separate explicit write here -- confirmed
         * from source now available (reference_dynamic_c/
         * ACTIVERFID_V1.02_UHF.c), closing out what was previously a
         * flagged "form/LED reset behavior not verified" gap. */
#if APP_ENABLE_DISPLAY
        display_activate_form(GENIE_FORM_MAIN);
#endif
        app_genie_update_main(app);
    } else {
        app->program_state = APP_STATE_IDLE;

        /* Position CONFIRMED from the original, ACTIVERFID_V1.02_UHF.c
         * line 1535, UHF_Reader_Control()'s disable branch:
         * `BitWrPortI(PBDR,&PBDRShadow,1,6); //solid green` -- the
         * second statement, right after ProgramState=IDLE and before
         * StopReaders(). Solid GREEN specifically (not just "on") per
         * explicit test request -- matches "green when we have time
         * sync" for the general not-reading state (this stop-reading
         * moment is always reached from an already-synced, already-
         * reading session in practice). */
        button_led_green();

#if APP_ENABLE_UHF
        uhf_reader_stop(&app->uhf);
#endif
        app->settings.shutdown_status = 0; /* NORMAL_SHUTDOWN */
        app->uhf_reading = 0;
        app_persist_settings(app);
        fan_off();
        /* Was the "stop reading" beep -- one raw pulse (same
         * unconditional-of-Settings.Beeper fidelity note as the
         * double-beep above; ACTIVERFID_V1.02_UHF.c lines 1540-1542).
         * CONFIRMED FIXED 2026-07-17, was previously app_beep_n(). */
        buzzer_beep_n_blocking(1);

        /* Was `genieWriteContrast(Settings.Brightness);
         * genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LOOP/PBACK/BTON/LPM,
         * 0);` -- confirmed from source now available, closing out the
         * same previously-flagged gap as the start branch above. The
         * original's `BitWrPortI(PBDR,&PBDRShadow,1,6)` (a raw GPIO,
         * not a Genie widget) is NOT ported here -- it's covered by
         * CLAUDE.md's existing BUTTON_LED open item (no driver built,
         * purpose only guessed from the pin name), not silently
         * dropped without a trace. */
#if APP_ENABLE_DISPLAY
        display_set_contrast(app->settings.brightness);
        display_set_led(DISPLAY_LED_LOOP, 0);
        display_set_led(DISPLAY_LED_PLAYBACK, 0);
        display_set_led(DISPLAY_LED_BT_ON, 0);
        display_set_led(DISPLAY_LED_LOW_POWER_MODE, 0);
#endif
    }
}

void app_dispatch_pc_command(app_context_t *app, int socket_index,
                              const tcp_socket_transport_t *reply_transport,
                              const pc_parsed_command_t *cmd)
{
    switch (cmd->id) {

    case PC_CMD_REWIND:
        app->rewind_type = cmd->params.rewind.type;
        app->rewind_from_time = cmd->params.rewind.from_time;
        app->rewind_to_time = cmd->params.rewind.to_time;
        app->rewind_socket_index = socket_index;
        app->only_rewind_unsent = 0;
        /* Was RWR_READING directly -- process_rewind() (app_loop.c) is
         * now a resumable state machine, added 2026-07-20: RWR_STARTING
         * means "open the rewind-dedicated handle + binary search once",
         * RWR_READING means "already positioned, stream a batch per
         * call". */
        app->rwr_state = RWR_STARTING;
        PRINTF("PC_CMD_REWIND received: type=%d from=%lu to=%lu socket=%d\r\n",
               (int)app->rewind_type, (unsigned long)app->rewind_from_time,
               (unsigned long)app->rewind_to_time, socket_index);
        break;

    case PC_CMD_STOP_REWIND:
        app->rwr_state = RWR_STOPPED;
        /* Added 2026-07-20 -- a rewind can now genuinely be mid-stream
         * (its own handle held open across many main-loop iterations),
         * so an explicit stop must close it here rather than relying on
         * process_rewind() noticing the state change on its next call
         * (which it also does, as a second line of defense, but not
         * doing it here would leave the handle open for however long
         * until that next call happens to run). */
        nand_log_rewind_close(&app->log);
        break;

    case PC_CMD_UHF_STOP:
        if (app->program_state == APP_STATE_READING && app->settings.system) {
            app_uhf_reader_control(app, 0);
        }
        break;

    case PC_CMD_UHF_START:
        if (app->program_state == APP_STATE_IDLE && app->settings.system) {
            app_uhf_reader_control(app, 1);
        }
        break;

    case PC_CMD_START_LIVE_DATA: {
        uint32_t from = cmd->params.start_live_data.from_time;
        if (from == 0) {
            from = app->settings.last_time_sent;
        }
        app->rewind_from_time = from;
        app->rewind_to_time = 0;
        app->only_rewind_unsent = 1;
        app->send_data_to_port = 1;
        app->rewind_socket_index = socket_index;
        /* Was StartDataSend() -- never explicitly sets RewindType in the
         * original either, but its own commented-out
         * `//RewindLogFile_BinarySearch(RWRFromTime, 0, 8);` shows the
         * intended type is 8 (REWIND_BY_TIME) -- RWRFromTime is always a
         * timestamp here, never a record number. Added explicitly
         * 2026-07-20: previously harmless since process_rewind() always
         * compared against date_time regardless of type (bug #1, now
         * fixed), so this path happened to work by accident; now that
         * the type is actually branched on, leaving it unset/stale from
         * a prior PC_CMD_REWIND call would break this path. */
        app->rewind_type = REWIND_BY_TIME;
        app->rwr_state = RWR_STARTING;
        break;
    }

    case PC_CMD_STOP_LIVE_DATA:
        app->send_data_to_port = 0;
        break;

    case PC_CMD_SET_TIME: {
        rtc_datetime_t dt;
        dt.year = cmd->params.datetime.year;
        dt.mon = cmd->params.datetime.mon;
        dt.mday = cmd->params.datetime.mday;
        dt.hour = cmd->params.datetime.hour;
        dt.min = cmd->params.datetime.min;
        dt.sec = cmd->params.datetime.sec;
        dt.wday = 0;
        dt = rtc_epoch_to_datetime(rtc_datetime_to_epoch(&dt));

        ds3231_rt1062_write(&dt);
        app->current_time = dt;
        app->set_time_nrf_pending = 1;

        {
            char buf[64];
            pc_datetime_fields_t f;
            int n;
            f.hour = dt.hour; f.min = dt.min; f.sec = dt.sec;
            f.mday = dt.mday; f.mon = dt.mon; f.year = dt.year;
            n = pc_format_datetime_reply(&f, (long)rtc_datetime_to_epoch(&dt), buf, sizeof(buf));
            if (n > 0 && reply_transport != NULL) {
                reply_transport->send(reply_transport->ctx, (const uint8_t *)buf, (size_t)n);
            }
        }
        break;
    }

    case PC_CMD_GET_TIME: {
        char buf[64];
        pc_datetime_fields_t f;
        int n;
        f.hour = app->current_time.hour; f.min = app->current_time.min; f.sec = app->current_time.sec;
        f.mday = app->current_time.mday; f.mon = app->current_time.mon; f.year = app->current_time.year;
        n = pc_format_datetime_reply(&f, (long)rtc_datetime_to_epoch(&app->current_time), buf, sizeof(buf));
        if (n > 0 && reply_transport != NULL) {
            reply_transport->send(reply_transport->ctx, (const uint8_t *)buf, (size_t)n);
        }
        break;
    }

    case PC_CMD_GET_READING_STATUS: {
        uint8_t buf[5];
        pc_build_reading_status(app->program_state == APP_STATE_READING,
                                 app->send_data_to_port, buf);
        if (reply_transport != NULL) {
            reply_transport->send(reply_transport->ctx, buf, sizeof(buf));
        }
        break;
    }

    case PC_CMD_GET_SETTINGS:
        if (reply_transport != NULL) {
            app_send_settings_dump(app, reply_transport);
        }
        break;

    case PC_CMD_SET_SETTINGS:
        break;

    case PC_CMD_REMOTE_CONFIG:
        /* Was ProcessRemoteConfigSettings(sBuffer) dispatched from here
         * too in the original -- but that's the GPRS/Outreach config
         * protocol (gprs_response_parser.h's rcfg_process_buffer), a
         * different wire format than the direct PC command set. If
         * your PC client also sends 0x03-prefixed config records
         * directly, wire rcfg_process_buffer() in here. */
        break;

    case PC_CMD_BATTERY_QUERY:
        break;

    case PC_CMD_SET_OUTPUT_TYPE:
        if (pc_validate_output_type(cmd->params.output_type)) {
            app->settings.output_type = cmd->params.output_type;
            app_persist_settings(app);
        }
        break;

    case PC_CMD_UNKNOWN:
    case PC_CMD_MALFORMED:
    default:
        break;
    }
}
