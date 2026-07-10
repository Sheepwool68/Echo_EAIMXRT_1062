/*
 * app_loop.c
 *
 * ==========================================================================
 * INTEGRATION SCAFFOLD -- same caveats as app_init.c. Broken into one
 * function per section of the original's for(;;) body, matching its
 * structure and ordering closely so it stays easy to diff against the
 * source if something behaves unexpectedly.
 * ==========================================================================
 *
 * Was main()'s for(;;) loop body.
 */

#include "app_context.h"
#include "app_pc_dispatch.h"
#include "display_stub.h"
#include "gps_stub.h"
#include "neo_m8t_transport_rt1062.h"
#include "buzzer_rt1062.h"
#include "finish_lynx_protocol.h"
#include "outreach.h"
#include "gprs_batch_sender.h"
#include "gprs_response_parser.h"
#include "gprs_config_record.h"
#include "rfid_logic.h"
#include "bringup_config.h"
#include "ms_time.h"
#include "systick_ms_rt1062.h"
#include <stdio.h>
#include <string.h>

#define BEEP_DELAY_MS      300u  /* on-time per pulse, and the "silence gap"
    threshold that lets a continuous run of reads sound like one solid
    tone instead of a stutter -- see uhf_event_cb()'s UHF_FRAME_TAG_READ
    case below for why. */
#define BEEP_GAP_MS        500u  /* gap between pulses in a multi-beep sequence
    (e.g. the two beeps for "start reading") -- NOT confirmed from source
    (only the single-pulse on-delay was given); using the same 500ms as a
    reasonable symmetric default. Adjust if your real firmware uses a
    different gap. */
#define BUZZER_PHASE_IDLE  0
#define BUZZER_PHASE_ON    1
#define BUZZER_PHASE_GAP   2
#define DIM_DELAY_MS       10000u /* was DIM_DELAY_S in the original --
    the exact value isn't recoverable from any source available to this
    port (a compacted part of this conversation once had visibility
    into a `trigger_dim`/`DIM_DELAY_S` fragment, but not the value
    itself). 10 seconds per explicit instruction for this port, not a
    recovered or derived value -- change freely if it doesn't match
    your actual firmware's behavior. */
#define DIM_DELAY_S        20u
#define BATTERY_CHECK_MS   10000u
#define GPS_SIGNAL_CHECK_MS 30000u
#define STATUS_BROADCAST_MS 2000u
#define GPRS_CHECK_TIME_MS  10000u
#define UHF_ANT_MASK_FULL   0x0F

/* ------------------------------------------------------------------ */
/* Section: TCP client sockets + PC command dispatch                    */
/* was ProcessDataSockets() -> ProcessDataSocket() -> CheckForPCCommands() */
/* ------------------------------------------------------------------ */

static void process_data_sockets(app_context_t *app, uint32_t now_ms)
{
    int i;

    tcp_lwip_listener_poll_accept(&app->tcp_listener);

    for (i = 0; i < TCP_LWIP_MAX_CLIENTS; i++) {
        pc_parsed_command_t cmd;
        tcp_session_event_t ev = tcp_session_process(
            &app->tcp_listener.transports[i],
            &app->tcp_listener.sessions[i],
            app->settings.last_time_sent,
            app->batt_percent,
            &cmd);

        if (ev == TCP_SESSION_COMMAND) {
            app_dispatch_pc_command(app, i, &app->tcp_listener.transports[i], &cmd);
        }
        /* TODO: if a slot's is_alive() has gone false, close+free its
         * fd so tcp_lwip_listener_poll_accept can reuse the slot --
         * see the TODO already flagged in tcp_transport_lwip.c. */
    }

    tcp_broadcast_status(app->tcp_listener.transports, app->tcp_listener.sessions,
                          TCP_LWIP_MAX_CLIENTS, app->batt_percent,
                          now_ms,
                          STATUS_BROADCAST_MS, &app->last_status_broadcast_ms);
}

/* ------------------------------------------------------------------ */
/* Section: FinishLynx integration                                      */
/* was TCPIPOpenSocket_FinishLynx() / ProcessDataSocket_FinishLynx() /   */
/* WriteStatusMessages_FinishLynx()                                     */
/* ------------------------------------------------------------------ */

#define FINISH_LYNX_TIME_SYNC_MS 300000u /* CONFIRMED: 5 minutes */

static void finish_lynx_read_current_time_parts(app_context_t *app, uint32_t now_ms,
                                                 int *out_hour, int *out_min, int *out_sec, int *out_ms)
{
    /* Was `ulTime = read_rtc(); mktm(&CurTime, ulTime);` -- a FRESH RTC
     * read each time this is called (not reusing app->current_time),
     * matching the original's own choice to re-read at the exact
     * moment of sending rather than rely on a possibly-stale cached
     * value refreshed elsewhere in the main loop. */
    rtc_datetime_t dt;
    uint32_t ms_since_edge;

    ds3231_rt1062_read(&dt);
    *out_hour = dt.hour;
    *out_min = dt.min;
    *out_sec = dt.sec;

    /* Was `milliseconds = MS_TIMER - iDSTimeFromRabbit;` -- a plain
     * subtraction, same underflow-prone class of bug as the beeper's
     * `MS_TIMER - BEEPDELAY > LastBeepTime` (see process_beeper_and_dim()'s
     * history). Using ms_elapsed() here for the same reason: fixed
     * rather than reproduced, since there's no reason to keep a bug
     * that only bites in a narrow window after boot/rollover. */
    ms_since_edge = ms_elapsed(now_ms, app->ds_last_edge_ms);
    *out_ms = (int)(ms_since_edge > 999u ? 999u : ms_since_edge); /* clamp --
        should never actually exceed ~999 under normal 1Hz rollover
        operation, but %03d expects a 0-999 value and this avoids ever
        printing a garbled 4+ digit number if something's gone wrong
        upstream (e.g. a missed rollover edge). */
}

static void process_finish_lynx_socket(app_context_t *app, uint32_t now_ms)
{
    int i;

    tcp_lwip_listener_poll_accept(&app->finish_lynx_listener);

    for (i = 0; i < TCP_LWIP_MAX_CLIENTS; i++) {
        tcp_socket_transport_t *t = &app->finish_lynx_listener.transports[i];

        if (!t->is_established(t->ctx)) {
            app->finish_lynx_client_was_connected[i] = 0;
            continue;
        }

        if (!app->finish_lynx_client_was_connected[i]) {
            /* Was the ClientIsConnected==0 branch: send the initial
             * time sync on connect. Was ALSO `tcp_keepalive(&socket, 0)`
             * here (disabling TCP keepalive) -- TODO: this port's lwIP
             * transport wrapper doesn't currently expose a
             * keepalive-disable call; flagged rather than silently
             * dropped. If you need this, extend tcp_transport_lwip.c
             * with a call using lwIP's own `tcp_pcb->so_options &=
             * ~SOF_KEEPALIVE` (or your SDK version's equivalent). */
            char buf[40];
            int hour, min, sec, ms;
            int n;

            finish_lynx_read_current_time_parts(app, now_ms, &hour, &min, &sec, &ms);
            n = finish_lynx_build_time_string(hour, min, sec, ms, buf, sizeof(buf));
            if (n > 0) {
                t->send(t->ctx, (const uint8_t *)buf, (size_t)n);
            }
            app->finish_lynx_client_was_connected[i] = 1;
        }
    }

    /* Was WriteStatusMessages_FinishLynx() -- periodic 5-minute re-sync.
     * The original nested this inside the single socket's
     * already-connected branch; generalized here to a plain periodic
     * check that fans out to every currently-connected FinishLynx
     * client, consistent with this port's multi-client listener (see
     * app_context.h's note on that generalization). Matches the
     * original's own behavior of resetting the timer regardless of
     * whether any client was actually connected to receive it. */
    if (ms_has_elapsed(now_ms, app->finish_lynx_last_time_sync_ms, FINISH_LYNX_TIME_SYNC_MS)) {
        char buf[40];
        int hour, min, sec, ms;
        int n;

        finish_lynx_read_current_time_parts(app, now_ms, &hour, &min, &sec, &ms);
        n = finish_lynx_build_time_string(hour, min, sec, ms, buf, sizeof(buf));
        if (n > 0) {
            for (i = 0; i < TCP_LWIP_MAX_CLIENTS; i++) {
                if (app->finish_lynx_client_was_connected[i]) {
                    tcp_socket_transport_t *t = &app->finish_lynx_listener.transports[i];
                    t->send(t->ctx, (const uint8_t *)buf, (size_t)n);
                }
            }
        }
        app->finish_lynx_last_time_sync_ms = now_ms;
    }
}

/* ------------------------------------------------------------------ */
/* Section: reset socket + UDP discovery                                */
/* was ProcessResetSocket()                                             */
/* ------------------------------------------------------------------ */

static void process_reset_socket(app_context_t *app)
{
    if (tcp_reset_triggered(&app->reset_transport)) {
        /* TODO: close+reopen app->tcp_listener and Finish Lynx sockets
         * (Finish Lynx not covered by any pasted library yet). */
        app->reset_transport.reopen_listen(app->reset_transport.ctx);
    }
    udp_service_discovery(&app->discovery_transport, app->mac_address);
}

/* ------------------------------------------------------------------ */
/* Section: beeper timeout + dim timeout                                */
/* ------------------------------------------------------------------ */

/* Was Beep(). See app_context.h's declaration comment on the
 * unconditional-no-setting-check behavior this preserves exactly. */
void app_beep(app_context_t *app, uint32_t now_ms)
{
    app->last_beep_time_ms = now_ms;
    app->buzzer_phase = BUZZER_PHASE_ON;
    buzzer_on();
}

void app_beep_n(app_context_t *app, uint32_t now_ms, int count)
{
    if (!app->settings.beeper || count <= 0) {
        return;
    }
    if (app->buzzer_phase == BUZZER_PHASE_IDLE) {
        app_beep(app, now_ms);
        count--;
    }
    app->beeps_pending += count;
}

static void process_beeper_and_dim(app_context_t *app, uint32_t now_ms)
{
    /* Non-blocking beep sequencer: ON for BEEP_DELAY_MS, then either
     * IDLE (nothing queued) or a BEEP_GAP_MS silent gap before the next
     * queued pulse turns back on -- see app_beep()/app_beep_n() above
     * for where sequences get started/queued. */
    switch (app->buzzer_phase) {
    case BUZZER_PHASE_ON:
        if (ms_has_elapsed(now_ms, app->last_beep_time_ms, BEEP_DELAY_MS)) {
            /* CONFIRMED: off is the same pin driven low. */
            buzzer_off();
            if (app->beeps_pending > 0) {
                app->last_beep_time_ms = now_ms;
                app->buzzer_phase = BUZZER_PHASE_GAP;
            } else {
                app->buzzer_phase = BUZZER_PHASE_IDLE;
            }
        }
        break;
    case BUZZER_PHASE_GAP:
        if (ms_has_elapsed(now_ms, app->last_beep_time_ms, BEEP_GAP_MS)) {
            app->beeps_pending--;
            app->last_beep_time_ms = now_ms;
            app->buzzer_phase = BUZZER_PHASE_ON;
            buzzer_on();
        }
        break;
    case BUZZER_PHASE_IDLE:
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Section: display touch-event dispatch + dim timeout                  */
/* ------------------------------------------------------------------ */

static void process_display_events(app_context_t *app, uint32_t now_ms)
{
#if APP_ENABLE_DISPLAY
    display_event_t ev;
    int any_event = 0;

    /* Drains the display's event queue and dispatches real GENIE
     * button/widget events -- this is the ONLY place that should call
     * display_dequeue_event(); it owns draining the queue for the
     * whole app. */
    while (display_dequeue_event(&ev)) {
        any_event = 1;

        /* Was the GENIE_SYSTEM branch of the touch-event handler --
         * see app_pc_dispatch.c's app_uhf_active_mode_toggle() doc
         * comment for the exact original logic this ports. The
         * original's own check was just `Event.reportObject.index ==
         * GENIE_SYSTEM` with no explicit object-type check either --
         * matched here for fidelity, not tightened with an
         * ev.object == GENIE_OBJ_4DBUTTON guard the original didn't
         * have. */
        if (ev.index == GENIE_SYSTEM) {
            app_uhf_active_mode_toggle(app, ev.data);
        }

        /* TODO: further GENIE_* button/index handlers belong here as
         * their original source becomes available -- this dispatch
         * loop is the general mechanism; GENIE_SYSTEM is just the
         * first confirmed one. */
    }

    if (app->settings.dim) {
        if (any_event) {
            app->last_touch_time_ms = now_ms;
            if (app->screen_dimmed) {
                /* Wake: restore brightness and return to whichever
                 * form was active immediately before dimming (captured
                 * below when dimming kicked in) -- not a fixed form. */
                display_set_contrast(app->settings.brightness);
                display_activate_form(app->form_before_dim);
                app->screen_dimmed = 0;
            }
        } else if (!app->screen_dimmed
                   && ms_has_elapsed(now_ms, app->last_touch_time_ms, DIM_DELAY_MS)) {
            /* Dim: capture the currently active form before navigating
             * away, so waking can restore it exactly. genie_current_form()
             * can be -1 if the display hasn't reported any form yet
             * (e.g. dimming kicks in unusually early) -- guard against
             * that rather than restoring a garbage form id later. */
            int current_form = display_get_current_form();
            app->form_before_dim = (current_form >= 0) ? current_form : GENIE_FORM_MAIN;
            display_activate_form(GENIE_FORM_SLEEP);
            display_set_contrast(0);
            app->screen_dimmed = 1;
        }
    }
#else
    (void)app;
    (void)now_ms;
#endif
}

/* ------------------------------------------------------------------ */
/* Section: GPS PPS edge + DS3231 1Hz rollover                          */
/* ------------------------------------------------------------------ */

static void process_time_sync(app_context_t *app)
{
#if APP_ENABLE_GPS
    /* Was the ISR's `pps = pps^1` toggle reaching a global directly --
     * now synced explicitly from the transport layer's own ISR-driven
     * state each iteration (see neo_m8t_transport_rt1062.h). */
    app->pps = neo_m8t_gps_get_pps_state();
#endif

    if (pps_edge_detected(app->pps, app->old_pps)) {
        if (pps_should_trigger_gps_sync(app->gps_time_set, app->settings.auto_set_gps_time)) {
            rtc_datetime_t gps_time;
            if (gps_sync_time_from_fix(&gps_time, app->settings.time_zone, app->settings.add30)) {
                app->gps_time_set = 1;
                app->current_time = gps_time;
                ds3231_rt1062_write(&gps_time);
                /* Was `iDSTimeFromRabbit = MS_TIMER;` right after computing
                 * the new time in the original's SetTime_uBlox() -- resets
                 * the DS3231 rollover reference point immediately after a
                 * GPS-driven time jump, since writing a new RTC value
                 * doesn't itself generate a rollover edge for
                 * ds3231_rt1062_poll_rollover() to catch naturally. */
                app->ds_last_edge_ms = systick_ms_now();
                app->set_time_nrf_pending = 1;
#if APP_ENABLE_DISPLAY
                display_set_digits(DISPLAY_DIGITS_HOUR, gps_time.hour);
                display_set_digits(DISPLAY_DIGITS_MIN, gps_time.min);
                {
                    char date_str[16];
                    snprintf(date_str, sizeof(date_str), "%02d-%02d-%04d",
                             gps_time.mday, gps_time.mon, gps_time.year);
                    display_set_string(GENIE_DATE_STR, date_str);
                }
#endif
            }
        }
#if APP_ENABLE_DISPLAY
        display_set_led(DISPLAY_LED_PPS, app->pps);
#endif
        app->old_pps = app->pps;
    }

    {
        uint32_t edge_ms;
        if (ds3231_rt1062_poll_rollover(&edge_ms)) {
            app->ds_last_edge_ms = edge_ms;

            if (ds3231_rt1062_read(&app->current_time) == 0) {
                time_display_actions_t actions =
                    time_sync_display_refresh_actions(&app->current_time);
#if APP_ENABLE_DISPLAY
                if (actions.refresh_seconds) {
                    display_set_digits(DISPLAY_DIGITS_SEC, app->current_time.sec);
                }
                if (actions.refresh_minutes) {
                    display_set_digits(DISPLAY_DIGITS_MIN, app->current_time.min);
                }
                if (actions.refresh_hours) {
                    display_set_digits(DISPLAY_DIGITS_HOUR, app->current_time.hour);
                }
                if (actions.refresh_date) {
                    char date_str[16];
                    snprintf(date_str, sizeof(date_str), "%02d-%02d-%04d",
                             app->current_time.mday, app->current_time.mon, app->current_time.year);
                    display_set_string(GENIE_DATE_STR, date_str);
                }
#else
                (void)actions;
#endif
            }

            {
                /* was `if(!BitRdPortI(PBDR,3)){ comms_NRF(0x08); set_time_nrf=0; }` --
                 * PBDR bit3 is now the nRF SPI ready/attention line,
                 * read directly rather than through the poll/retrieve
                 * dispatch below (this is a quick non-blocking peek,
                 * not a full transaction). */
#if APP_ENABLE_NRF_SPI
                int spi_bus_busy = app->nrf_transport.read_ready_line(app->nrf_transport.hw_ctx);
#else
                int spi_bus_busy = 1; /* treat as always-busy with the
                    link disabled, so time_sync_maybe_push_nrf_time()
                    never fires and set_time_nrf_pending just stays set
                    until this stage is enabled -- matches the spirit of
                    "everything still runs, just inert" from the other
                    staged bring-up flags. */
#endif
                if (time_sync_maybe_push_nrf_time(&app->set_time_nrf_pending, spi_bus_busy)) {
                    uint32_t nrf_time = rtc_datetime_to_nrf_time(&app->current_time);
#if APP_ENABLE_NRF_SPI
                    nrf_spi_send_datetime(&app->nrf_transport, nrf_time);
#else
                    (void)nrf_time;
#endif
                }
            }

            /* MP2731 battery-charger watchdog kick / charge-status poll:
             * not ported (separate library, never provided). Original
             * piggy-backed this onto the same 1Hz tick -- recommend
             * keeping that cadence if/when you port it, but it doesn't
             * belong architecturally inside time-sync; consider its own
             * periodic check instead. */
        }
    }
}

/* ------------------------------------------------------------------ */
/* Section: UHF reading                                                 */
/* was `if(uhf_reading){ TM_ReadSerialPort(3); TM_ProcessChipArray(); }` */
/* ------------------------------------------------------------------ */

typedef struct {
    app_context_t *app;
    uint32_t now_ms;
} uhf_event_ctx_t;

static void uhf_event_cb(void *ctx, const uhf_frame_event_t *ev)
{
    uhf_event_ctx_t *c = (uhf_event_ctx_t *)ctx;
    app_context_t *app = c->app;

    switch (ev->type) {
    case UHF_FRAME_TAG_READ: {
        size_t idx;
        uhf_chip_add_result_t r = uhf_chip_array_add(
            app->uhf_chips, UHF_CHIP_ARRAY_SIZE,
            ev->data.tag.chip_code, ev->data.tag.rssi, ev->data.tag.antenna,
            (uint32_t)rtc_datetime_to_epoch(&app->current_time), app->ds_last_edge_ms,
            1 /* fix_rssi_update_bug -- recommend the fixed behavior; see
                 uhf_chip_array.h's porting notes if you'd rather match
                 the original's bug exactly */,
            &app->uhf_chip_count, &app->uhf_unique_chips, &idx);
        (void)r;

        /* Beep on the READ itself, not on the record later being
         * logged/flushed -- gated on Settings.Beeper here since
         * app_beep() itself is unconditional (matches Beep() exactly).
         * Calling app_beep() directly (not app_beep_n()) re-arms the
         * same on-timer on every read rather than queuing a discrete
         * pulse: an isolated read gives one ~300ms pulse, but a rapid
         * run of reads (many chips in range at once) keeps re-arming
         * the timer before it can turn off, so the buzzer holds
         * solidly on for as long as reads keep coming in -- not a
         * stutter of individual pulses. */
        if (app->settings.beeper) {
            app_beep(app, c->now_ms);
        }
        break;
    }
    case UHF_FRAME_ANT_STATUS:
        app->uhf.ants = ev->data.ant_status_mask;
        break;
    case UHF_FRAME_RETURN_LOSS:
        if (ev->data.return_loss.good) {
            app->uhf.ants |= ev->data.return_loss.ant_bit;
        }
        break;
    case UHF_FRAME_END_OF_ROUND:
    case UHF_FRAME_HEARTBEAT:
    case UHF_FRAME_START_CONFIRM:
    case UHF_FRAME_TEMPERATURE:
    default:
        break;
    }
}

static void process_uhf_reading(app_context_t *app, uint32_t now_ms)
{
    if (!app->uhf_reading) {
        return;
    }

    {
        uint8_t buf[1024];
        int n = app->uhf_transport.read(app->uhf_transport.ctx, buf, sizeof(buf), 3);
        if (n > 0) {
            uhf_event_ctx_t ctx = { app, now_ms };
            uhf_process_buffer(buf, (size_t)n, uhf_event_cb, &ctx);
        }
    }

    {
        nrf_record_t records[UHF_CHIP_FLUSH_BATCH_MAX];
        size_t n = uhf_chip_array_flush_aged(
            app->uhf_chips, UHF_CHIP_ARRAY_SIZE,
            (uint32_t)rtc_datetime_to_epoch(&app->current_time),
            records, UHF_CHIP_FLUSH_BATCH_MAX, app->last_log_id + 1);

        if (n > 0) {
            size_t i;
            /* Beep trigger moved to uhf_event_cb()'s UHF_FRAME_TAG_READ
             * case above -- beeps on the read, not on the record being
             * logged/flushed here. */
            app->last_log_id += (uint32_t)n;
            app->chip_reads = (uint8_t)(app->chip_reads + n);

            /* Was WriteChipToSocket() per record: build the CSV line
             * (reusing rfid_create_sock_string from the very first
             * module of this port) and fan it out to every connected
             * TCP client. */
            for (i = 0; i < n; i++) {
                char line[100];
                uint32_t chip_code_value = 0;
                int len;

                /* BUG FOUND ON REVIEW: rfid_create_sock_string's
                 * uhf_code_value parameter is the actual numeric chip
                 * code to display -- extracted here from xpdr_code[2..5]
                 * (big-endian), matching the convention established
                 * back in the very first module of this port. */
                if (records[i].xpdr_code[0] == 0) {
                    chip_code_value = ((uint32_t)(uint8_t)records[i].xpdr_code[2] << 24)
                                     | ((uint32_t)(uint8_t)records[i].xpdr_code[3] << 16)
                                     | ((uint32_t)(uint8_t)records[i].xpdr_code[4] << 8)
                                     | (uint32_t)(uint8_t)records[i].xpdr_code[5];
                }

                len = rfid_create_sock_string(&records[i], 0, line, sizeof(line),
                                               chip_code_value, 1,
                                               app->settings.channel,
                                               (output_type_t)app->settings.output_type,
                                               0);
                if (len > 0) {
                    int s;
                    for (s = 0; s < TCP_LWIP_MAX_CLIENTS; s++) {
                        if (app->tcp_listener.sessions[s].client_connected) {
                            app->tcp_listener.transports[s].send(
                                app->tcp_listener.transports[s].ctx,
                                (const uint8_t *)line, (size_t)len);
                        }
                    }
                }

                /* Was CreateSockString_FinishLynx() -- inferred call
                 * site: the original wasn't pasted with its own caller
                 * visible, but its signature (takes a whole nrf_record,
                 * returns a formatted line) matches this exact
                 * "broadcast per newly-logged record" point, mirroring
                 * the CSV broadcast just above. Flagged as inferred,
                 * not verified from a direct paste of this call site. */
#if APP_ENABLE_TCP
                {
                    char fl_line[40];
                    int fl_len = finish_lynx_build_split_string(&records[i], 0, fl_line, sizeof(fl_line));
                    if (fl_len > 0) {
                        int s;
                        for (s = 0; s < TCP_LWIP_MAX_CLIENTS; s++) {
                            if (app->finish_lynx_client_was_connected[s]) {
                                app->finish_lynx_listener.transports[s].send(
                                    app->finish_lynx_listener.transports[s].ctx,
                                    (const uint8_t *)fl_line, (size_t)fl_len);
                            }
                        }
                    }
                }
#endif
            }

            nand_log_open_for_append(&app->log);
            nand_log_append_records(&app->log, records, n);
            nand_log_close(&app->log);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Section: Remote/GPRS processing                                      */
/* was Remote_Process() -> Remote_Main()                                */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Wrappers binding gprs_batch_sender.h's function-pointer seams to    */
/* this app's actual storage/transport                                  */
/* ------------------------------------------------------------------ */

static int gprs_nand_seek_wrapper(void *ctx, uint64_t record_index)
{
    return nand_log_seek_to_record((nand_log_t *)ctx, record_index);
}

static int gprs_nand_read_wrapper(void *ctx, nrf_record_t *out)
{
    return nand_log_read_next_record((nand_log_t *)ctx, out);
}

static int gprs_modem_sink_wrapper(void *ctx, const uint8_t *buf, size_t len)
{
    app_context_t *app = (app_context_t *)ctx;
    return app->gprs_transport.write(app->gprs_transport.ctx, buf, len);
}

static int gprs_lan_sink_wrapper(void *ctx, const uint8_t *buf, size_t len)
{
    tcp_socket_transport_t *t = (tcp_socket_transport_t *)ctx;
    return t->send(t->ctx, buf, len);
}

/* ------------------------------------------------------------------ */
/* Remote config record dispatch (was ProcessRemoteConfigSettings)      */
/* ------------------------------------------------------------------ */

static void rcfg_event_dispatch(void *ctx, const rcfg_event_t *ev)
{
    app_context_t *app = (app_context_t *)ctx;

    switch (ev->type) {
    case RCFG_EVT_BEEPER_SET:
        app->settings.beeper = ev->beeper_on;
        /* Deliberately NOT persisted (see settings_store.h's flash-wear
         * caution): a remote config record can arrive frequently, and
         * auto-saving on every one would wear the flash block holding
         * the settings file much faster than user-initiated changes
         * (PC commands, app_pc_dispatch.c) ever would. This value
         * takes effect for the current session only, unless something
         * else triggers a save later. Revisit if you find you actually
         * need this to survive a reset. */
        break;
    case RCFG_EVT_SEND_TO_REMOTE_SET:
        app->settings.send_data_to_remote_server = ev->send_to_remote_value;
        /* Same deliberate non-persistence as RCFG_EVT_BEEPER_SET above. */
        break;
    case RCFG_EVT_STOP_READING:
        if (app->program_state == APP_STATE_READING && app->settings.system) {
            app_uhf_reader_control(app, 0);
        }
        break;
    case RCFG_EVT_START_READING:
        if (app->program_state == APP_STATE_IDLE && app->settings.system) {
            app_uhf_reader_control(app, 1);
        }
        break;
    case RCFG_EVT_REWIND: {
        pc_rewind_params_t params;
        if (pc_parse_rewind_command(ev->rewind_data, ev->rewind_data_len, &params) == 1) {
            app->rewind_type = params.type;
            app->rewind_from_time = params.from_time;
            app->rewind_to_time = params.to_time;
            /* FLAGGED GAP: a GPRS/Outreach-originated rewind should
             * reply over the SAME channel it arrived on, not a TCP
             * client slot -- but process_rewind() (below) only knows
             * how to write to app->tcp_listener.transports[N] today.
             * Setting -1 here makes process_rewind() skip the actual
             * socket write (its bounds check already guards against an
             * out-of-range index) rather than write to the wrong place,
             * but the rewound data effectively goes nowhere until this
             * is extended to support a GPRS-transport reply path too. */
            app->rewind_socket_index = -1;
            app->only_rewind_unsent = 0;
            app->rwr_state = RWR_READING;
        }
        break;
    }
    case RCFG_EVT_NOOP:
    default:
        break;
    }
}

static void gprs_rx_event_dispatch(void *ctx, const gprs_rx_event_t *ev)
{
    app_context_t *app = (app_context_t *)ctx;

    if (ev->type == GPRS_RX_OK_ACK) {
        if (ev->ack_valid) {
            app->gprs_base_record = ev->ack_record_no;
        }
    } else if (ev->type == GPRS_RX_CONFIG_RECORD) {
        int mark_outreach = 0;
        rcfg_process_buffer(ev->config_record_data, ev->config_record_len,
                             rcfg_event_dispatch, app, &mark_outreach);
        /* TODO: mark_outreach corresponds to the original's
         * iClientType[iRewindSocket]=1 side effect -- connection-type
         * bookkeeping for Finish Lynx/Outreach client differentiation
         * that isn't modeled in tcp_socket_session_t yet. */
        (void)mark_outreach;
    }
}

static void remote_main(app_context_t *app, uint32_t now_ms)
{
    if (app->modem.gprs_state == GPRS_STATE_NOGPRS) {
        app->modem.gprs_status = GPRS_STATUS_CON;
        /* Was outbound_connect_to_socket_server(app) called directly
         * (blocking, in an earlier version of this port -- this whole
         * module was also named outbound_connect.c/.h at the time,
         * later renamed to outreach.c/.h) -- now only STARTS an
         * attempt if one isn't already mid-flight. outreach_step()
         * (called unconditionally every iteration, see
         * app_run_one_iteration()) is what actually advances it; this
         * call site is purely the rate-limited "should we begin a new
         * attempt" gate, matching the original's own retry cadence via
         * gprs_wait_time_ms. */
        if (!outreach_in_progress(app)) {
            outreach_begin(app, now_ms);
        }
    } else if (app->modem.gprs_state == GPRS_STATE_DISCONNECTED) {
        gprs_modem_toggle(&app->modem, app->settings.remote_type != 0);
        app->modem.gprs_state = GPRS_STATE_NOGPRS;
    } else if (app->modem.gprs_state == GPRS_STATE_CONNECTED) {
        if (app->settings.send_data_to_remote_server) {
            gprs_batch_sender_config_t cfg;
            memset(&cfg, 0, sizeof(cfg));
            cfg.source_ctx = &app->log;
            cfg.seek = gprs_nand_seek_wrapper;
            cfg.read = gprs_nand_read_wrapper;
            /* Was Remote_SendNextBatch()'s implicit choice of transport --
             * now genuinely resolved (previously a TODO/gap): pick the
             * modem or LAN sink based on which one is actually active,
             * matching Settings.RemoteType's own selection. */
            if (app->settings.remote_type == 2) {
                cfg.sink_ctx = &app->lan_client_transport;
                cfg.sink = gprs_lan_sink_wrapper;
            } else {
                cfg.sink_ctx = app;
                cfg.sink = gprs_modem_sink_wrapper;
            }
            cfg.channel = app->settings.channel;
            memcpy(cfg.mac_address, app->mac_address, 6);
            gprs_send_next_batch(&cfg, &app->settings.gprs_current_rec);
        }

        {
            /* Was Remote_CheckForResponse() */
            uint8_t buf[512];
            int n = app->gprs_transport.read(app->gprs_transport.ctx, buf, sizeof(buf), 100);
            if (n > 0) {
                gprs_process_response_buffer(buf, (size_t)n, gprs_rx_event_dispatch, app);
            }
        }

        {
            /* Was Remote_SendConfigRecord() */
            gprs_config_inputs_t in;
            gprs_remote_config_rec_t rec;

            memset(&in, 0, sizeof(in));
            in.ants = app->uhf.ants;
            in.reader_power = app->settings.reader_power;
            in.beeper = app->settings.beeper;
            in.channel = app->settings.channel;
            in.time_zone = (int16_t)app->settings.time_zone;
            in.rtc_seconds = (uint32_t)rtc_datetime_to_epoch(&app->current_time);
            in.send_data_to_remote = app->settings.send_data_to_remote_server;
            memcpy(in.rabbit_ip, app->settings.rabbit_ip, 4);
            memcpy(in.mac_address, app->mac_address, 6);
            in.is_reading = (app->program_state == APP_STATE_READING);
            in.uhf_system_mode = app->settings.system;
            in.battery_percent = (int16_t)app->batt_percent;
            in.chip_reads = app->chip_reads;
            /* in.gps_coords left zeroed -- GPS coordinate reporting
             * depends on NEOM8T.lib, not yet ported. */

            gprs_build_config_record(&in, &rec);
            app->gprs_transport.write(app->gprs_transport.ctx, (const uint8_t *)&rec, sizeof(rec));
        }
    }
}

static void process_remote(app_context_t *app, uint32_t now_ms)
{
    if (app->settings.remote_type) {
        if (ms_has_elapsed(now_ms, app->gprs_last_process_time_ms, app->gprs_wait_time_ms)) {
            if (app->modem.gprs_state == GPRS_STATE_NOGPRS) {
                app->modem.gprs_status = GPRS_STATUS_CON;
            }
            remote_main(app, now_ms);
            app->gprs_last_process_time_ms = now_ms;
        }
    } else {
#if APP_ENABLE_DISPLAY
        display_set_led(DISPLAY_LED_GPRS_SOCKET, 0);
#endif
    }
}

/* ------------------------------------------------------------------ */
/* Section: periodic battery status + GPS signal check                  */
/* ------------------------------------------------------------------ */

static void process_periodic_checks(app_context_t *app, uint32_t now_ms)
{
    if (now_ms > app->check_interval_ms) {
        /* TODO: battery ADC read (MP2731/charger library not provided);
         * app->batt_percent left as whatever the caller last set. */
        app->check_interval_ms = now_ms + BATTERY_CHECK_MS;
    }

    if (app->gps_time_set) {
        if (now_ms > app->check_interval2_ms) {
            gps_update_signal_status();
            app->check_interval2_ms = now_ms + GPS_SIGNAL_CHECK_MS;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Section: rewind-while-reading                                        */
/* was `costate { wfd RewindLogFile_BinarySearch(8); abort; }`          */
/* ------------------------------------------------------------------ */

static void process_rewind(app_context_t *app)
{
    if (app->rwr_state != RWR_READING) {
        return;
    }

    {
        uint32_t record_no;
        if (nand_log_open_for_read(&app->log) == 0) {
            if (nand_log_binary_search(&app->log, app->rewind_from_time,
                                        app->rewind_type, &record_no) == 1) {
                nrf_record_t rec;
                while (nand_log_read_next_record(&app->log, &rec) == 1) {
                    int stop = 0;
                    if (app->rewind_from_time == 0
                        || (rec.date_time >= app->rewind_from_time
                            && (app->rewind_to_time == 0 || rec.date_time <= app->rewind_to_time))) {
                        if (!app->only_rewind_unsent || !rec.has_been_sent) {
                            char line[100];
                            uint32_t chip_code_value = 0;
                            int len;
                            int is_uhf_rec = (rec.xpdr_code[0] == 0);

                            if (is_uhf_rec) {
                                chip_code_value = ((uint32_t)(uint8_t)rec.xpdr_code[2] << 24)
                                                 | ((uint32_t)(uint8_t)rec.xpdr_code[3] << 16)
                                                 | ((uint32_t)(uint8_t)rec.xpdr_code[4] << 8)
                                                 | (uint32_t)(uint8_t)rec.xpdr_code[5];
                            }

                            len = rfid_create_sock_string(&rec, 1, line, sizeof(line),
                                                           chip_code_value, is_uhf_rec,
                                                           app->settings.channel,
                                                           (output_type_t)app->settings.output_type,
                                                           0);
                            if (len > 0 && app->rewind_socket_index >= 0
                                && app->rewind_socket_index < TCP_LWIP_MAX_CLIENTS) {
                                app->tcp_listener.transports[app->rewind_socket_index].send(
                                    app->tcp_listener.transports[app->rewind_socket_index].ctx,
                                    (const uint8_t *)line, (size_t)len);
                            }
                        }
                    } else if (app->rewind_to_time != 0 && rec.date_time > app->rewind_to_time) {
                        stop = 1;
                    }
                    if (stop) {
                        break;
                    }
                }
            }
            nand_log_close(&app->log);
        }
    }

    app->rwr_state = RWR_STOPPED;
}

/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Section: nRF52833 SPI poll/retrieve dispatch + one-off commands       */
/* was `if(BitRdPortI(PBDR,3)){ comms_NRF(spi_record_state); }` +       */
/* `if(encode_tag){ comms_NRF(0x05); }` +                               */
/* `if(transmitter_off){ comms_NRF(0x0C); transmitter_off=0; }`         */
/* ------------------------------------------------------------------ */

#if APP_ENABLE_NRF_SPI
static void process_nrf_spi(app_context_t *app)
{
    if (app->nrf_transport.read_ready_line(app->nrf_transport.hw_ctx)) {
        if (app->spi_record_state == 1) {
            uint8_t count = 0, record_type = 0;
            nrf_spi_status_t st = nrf_spi_poll(&app->nrf_transport, &count, &record_type);

            if (st == NRF_SPI_OK && count > 0) {
                app->nrf_pending_record_count = count;
                app->nrf_pending_record_type = record_type;
                app->spi_record_state = 2; /* was the original's transition
                    to the retrieve phase on a successful, nonzero poll */
            }
            /* else: stay in state 1 -- was the original's implicit
             * behavior of leaving spi_record_state untouched on an
             * empty poll or error, trying again next iteration */

        } else { /* spi_record_state == 2: retrieve */
            nrf_record_t records[NRF_SPI_MAX_RECORDS];
            int n;

            if (app->nrf_pending_record_type == 1) {
                n = nrf_spi_retrieve_live_records(&app->nrf_transport,
                                                   app->nrf_pending_record_count,
                                                   records, NRF_SPI_MAX_RECORDS);
            } else {
                char code[7] = {0};
                uint32_t boots = 0, bt_time = 0;
                uint8_t fw = 0;
                n = nrf_spi_retrieve_playback_records(&app->nrf_transport,
                                                        app->nrf_pending_record_count,
                                                        records, NRF_SPI_MAX_RECORDS,
                                                        code, &boots, &bt_time, &fw);
            }
            app->spi_record_state = 1; /* was the original's unconditional
                reset back to the poll phase after every retrieve attempt */

            if (n > 0) {
                int i;
                for (i = 0; i < n; i++) {
                    char line[100];
                    int len;

                    app->last_log_id++;
                    records[i].log_id = app->last_log_id;
                    app->chip_reads = (uint8_t)(app->chip_reads + 1);

                    /* Was WriteChipToSocket() -- Active/LF records are
                     * is_uhf=0, matching the rfid_create_sock_string
                     * convention already established for UHF records
                     * in process_uhf_reading() above. */
                    len = rfid_create_sock_string(&records[i], 0, line, sizeof(line),
                                                   0, 0,
                                                   app->settings.channel,
                                                   (output_type_t)app->settings.output_type,
                                                   0);
                    if (len > 0) {
                        int s;
                        for (s = 0; s < TCP_LWIP_MAX_CLIENTS; s++) {
                            if (app->tcp_listener.sessions[s].client_connected) {
                                app->tcp_listener.transports[s].send(
                                    app->tcp_listener.transports[s].ctx,
                                    (const uint8_t *)line, (size_t)len);
                            }
                        }
                    }
                }

#if APP_ENABLE_STORAGE
                nand_log_open_for_append(&app->log);
                nand_log_append_records(&app->log, records, (size_t)n);
                nand_log_close(&app->log);
#endif
            }
        }
    }

    if (app->encode_tag) {
        uint8_t crc = 0;
        /* Was comms_NRF(0x05) -- program a transponder's 6-byte code.
         * app->rfid_cmd is populated by whichever caller set encode_tag
         * (originally the touchscreen keyboard entry flow -- not
         * ported, since it depends on GENIE2.LIB; wire your own
         * trigger for app->encode_tag/app->rfid_cmd until then). */
        nrf_spi_program_chip_code(&app->nrf_transport, app->rfid_cmd.chip_code, &crc);
        app->encode_tag = 0;
    }

    if (app->transmitter_off_pending) {
        nrf_spi_transmitter_off(&app->nrf_transport);
        app->transmitter_off_pending = 0;
    }
}
#endif /* APP_ENABLE_NRF_SPI */

void app_run_one_iteration(app_context_t *app)
{
    uint32_t now_ms = systick_ms_now(); /* was the hardcoded `now_ms = 0`
        placeholder -- every timer-based feature below (beeper-off delay,
        status broadcast interval, GPRS process interval, battery/GPS
        check intervals) was silently non-functional until this was wired
        in, not just "not yet implemented." */

#if APP_ENABLE_TCP
    /* Drives lwIP's internal timers -- consolidated replacement for the
     * many scattered tcp_tick(NULL) calls in the original. Ethernet RX
     * itself is pumped by your board's ENET driver separately (see
     * tcp_transport_lwip.h's tcp_lwip_poll() doc comment). */
    tcp_lwip_poll();
    process_data_sockets(app, now_ms);
    process_reset_socket(app);
    process_finish_lynx_socket(app, now_ms);
#endif

#if APP_ENABLE_DISPLAY
    display_do_events();
#endif

    process_display_events(app, now_ms); /* drains the display's event
        queue (GENIE_SYSTEM toggle dispatch, dim timeout) -- must run
        after display_do_events() so events are actually available */

    process_beeper_and_dim(app, now_ms); /* just GPIO/timer comparisons,
        no hardware dependency gated behind a flag -- safe to always run */

#if APP_ENABLE_TIME_SYNC
    process_time_sync(app);
#endif

#if APP_ENABLE_UHF && APP_ENABLE_STORAGE
    process_uhf_reading(app, now_ms);
#endif

#if APP_ENABLE_GPRS && APP_ENABLE_STORAGE
    /* Advances any IN-PROGRESS connection attempt one step, every
     * iteration, regardless of the rwr_state gate below -- matches a
     * cofunc's own "once started, keeps running via yields until done"
     * semantics. Only the DECISION to START a new attempt (in
     * remote_main(), via process_remote() below) respects that gate;
     * an attempt already under way should not just stall because
     * rwr_state changed mid-flight. */
    if (outreach_in_progress(app)) {
        outreach_step(app, now_ms);
    }

    if (app->rwr_state != RWR_READING) {
        process_remote(app, now_ms);
    }
#endif

    /* Battery-check portion is currently a TODO/no-op; GPS-signal
     * portion already self-gates at runtime via app->gps_time_set,
     * which only ever becomes true once APP_ENABLE_GPS work is done --
     * so this call is always safe, no #if needed. */
    process_periodic_checks(app, now_ms);

#if APP_ENABLE_NRF_SPI
    process_nrf_spi(app);
#endif

#if APP_ENABLE_REWIND && APP_ENABLE_STORAGE && APP_ENABLE_TCP
    process_rewind(app);
#endif
}
