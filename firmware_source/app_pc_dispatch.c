#include "app_pc_dispatch.h"
#include "display_stub.h"
#include "civil_time.h"
#include "bringup_config.h"
#include "settings_store.h"
#include "systick_ms_rt1062.h"
#include "reader_shutdown_rt1062.h"
#include "fan_rt1062.h"
#include <string.h>
#include <stdio.h>

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
 * the scanning loop" operation. Confirmed from the actual pasted
 * source:
 *
 *   if(indata==0){  //switched off UHF from Other Toggle
 *      serEclose;
 *      BitWrPortI(PBDR, &PBDRShadow, 1, 4);    //turn off reader
 *      comms_NRF(0x03);    //set the LF power on the nrf52833
 *   }else{
 *      BitWrPortI(PBDR, &PBDRShadow, 0, 4);    //turn on reader
 *      Open_Reader();
 *   }
 *
 * uhf_enabled matches the original's `indata` truthiness (0 = off,
 * nonzero = on).
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
        /* Was comms_NRF(0x03) -- same command already mapped
         * elsewhere in this port (see app_init.c's Active/LF-mode
         * branch) to pushing the configured reader power, not a
         * literal power_percent=0x03 argument. */
        nrf_spi_set_reader_power(&app->nrf_transport, app->settings.reader_power);
#endif
    } else {
        reader_power_set(1); /* SHUTDOWN low + PWR high -- both pins together */
#if APP_ENABLE_UHF
        uhf_reader_open(&app->uhf, &app->uhf_transport);
#endif
    }
}

/* Was UHF_Reader_Control() */
void app_uhf_reader_control(app_context_t *app, int enable)
{
    uint32_t now_ms = systick_ms_now();

    if (enable) {
        app->program_state = APP_STATE_READING;

        /* Was the FAN_CONTROL pin write tied to reader start/stop --
         * confirmed active high, unconditional (not gated behind
         * APP_ENABLE_UHF, same as the beep trigger below, since it's
         * a plain GPIO output unrelated to the UHF hardware bring-up
         * stage). */
        fan_on();

        /* Was the "starting reading" beep trigger -- two pulses,
         * fired unconditionally on entering this branch (matches
         * Beep()'s own call sites gating on Settings.Beeper at the
         * call site, not inside the beep itself; see
         * app_beep_n()'s doc comment). Not conditioned on the later
         * antenna check succeeding -- if your real firmware only
         * beeps on confirmed antenna presence rather than on the
         * start command itself, move this below that check instead. */
        app_beep_n(app, now_ms, 2);

#if APP_ENABLE_UHF
        /* Was Open_Reader() + TM_InitialiseReader() -- the original
         * reopened the UART and re-ran the full init sequence every
         * time reading started, rather than keeping the link open
         * continuously; matched here for fidelity. */
        uhf_reader_open(&app->uhf, &app->uhf_transport);
        uhf_reader_initialise(&app->uhf, (uhf_region_t)app->settings.uhf_region,
                               app->settings.channel, app->uhf_mode);
#endif

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
            app->settings.shutdown_status = 1; /* ABNORMAL_SHUTDOWN --
                persisted so an unclean power-loss auto-resumes reading
                on next boot (see app_init.c) */
            app_persist_settings(app);
        }
    } else {
        app->program_state = APP_STATE_IDLE;
#if APP_ENABLE_UHF
        uhf_reader_stop(&app->uhf);
#endif
        app->settings.shutdown_status = 0; /* NORMAL_SHUTDOWN */
        app->uhf_reading = 0;
        app_persist_settings(app);
        fan_off();
        /* Was the "stop reading" beep trigger -- one pulse. */
        app_beep_n(app, now_ms, 1);
    }
    /* display_activate_form()/display_set_contrast()/display_set_led()
     * are all real now (see display_stub.c) -- but the EXACT form/LED
     * reset behavior the original's UHF_Reader_Control() performed
     * here isn't something this port has verified source for (that
     * detail lives in main()/program_init(), not GENIE.LIB itself,
     * and wasn't part of what was pasted). Paste that section if you
     * want this wired to match exactly; guessing specific form/LED
     * values here would risk a wrong-but-plausible-looking reset. */
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
        app->rwr_state = RWR_READING;
        break;

    case PC_CMD_STOP_REWIND:
        app->rwr_state = RWR_STOPPED;
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
        app->rwr_state = RWR_READING;
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
