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
#include "app_genie_dispatch.h"
#include "display_stub.h"
#include "gps_stub.h"
#include "neo_m8t_transport_rt1062.h"
#include "buzzer_rt1062.h"
#include "button_led_rt1062.h"
#include "finish_lynx_protocol.h"
#include "outreach.h"
#include "gprs_batch_sender.h"
#include "gprs_response_parser.h"
#include "gprs_config_record.h"
#include "rfid_logic.h"
#include "bringup_config.h"
#include "ms_time.h"
#include "systick_ms_rt1062.h"
#include "enet_lwip_rt1062.h"
#include "max17303_fuel_gauge_rt1062.h"
#include "debug_console_rt1062.h"
#include <stdio.h>
#include <string.h>

/* PRINTF redirect to LPUART5 -- see debug_console_rt1062.h and
 * hello_world.c's own redirect comment for the full "why" (semihosting
 * PRINTF blocks forever if the SWD/LPC-Link debug connection drops).
 * Needed here for the Genie touch-event trace in process_display_events()
 * below -- lpuart5_console_rt1062_init() must have already run (in
 * main()) before any PRINTF call in this file fires. */
#undef PRINTF
/* SILENCED (default) 2026-07-21, per explicit request ("printf on
 * ethernet comms only after boot") -- was `debug_printf`. This file
 * mixes tracing for many subsystems (Genie/display, UHF, GPS, rewind)
 * alongside the ENET/TCP tracing that's still wanted, so instead of
 * silencing ~20 scattered call sites individually, PRINTF itself now
 * defaults to a no-op and a second, always-on macro (ENET_PRINTF,
 * below) covers just the ENET/TCP lines. Restore `#define PRINTF
 * debug_printf` here to bring every other subsystem's tracing back. */
#define PRINTF(...) ((void)0)
/* Always-on -- ENET/TCP tracing only (ENET link up, DHCP lease, TCP
 * slot connect/disconnect). See PRINTF's own comment just above. */
#define ENET_PRINTF debug_printf

/* How long to hold the continuous chip-read activity buzzer on after
 * the LAST read before switching it off. CORRECTED 2026-07-17 to 300 --
 * the initial 200 was an approximate value given before the real source
 * was checked; user then pasted the actual original main-loop line
 * (`if(MS_TIMER - BEEPDELAY > LastBeepTime)
 * BitWrPortI(PBDR,&PBDRShadow,0,2);`), confirming CONFIRMED from source
 * (ACTIVERFID_V1.02_UHF.c line 62): `#define BEEPDELAY 300  //time the
 * buzzer is left on after chip(s) read`. That original line is also
 * worth noting for its OWN shape, not just its constant: unconditional,
 * a single check with no queue/gap-phase concept at all -- exactly the
 * simplified, dedicated mechanism this was rewritten into (see
 * process_uhf_read_buzzer() below), not an approximation of it. */
#define UHF_READ_BUZZER_TIMEOUT_MS 300u
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

#if APP_ENABLE_TCP
/* Diagnostic added 2026-07-16, first real hardware pass with
 * APP_ENABLE_TCP live: enet_lwip_rt1062_init()'s dhcp_start() is
 * non-blocking (see its own header comment) and nothing anywhere in
 * this port ever printed whether/when a lease actually landed, or what
 * IP was assigned -- same "silent success (or silent hang) is
 * indistinguishable from working-but-quiet" gap already hit once for
 * the Genie display (see app_init.c's display-detection PRINTF) and
 * again for the DS3231 TIMEPULSE ISR. Without this, a user trying to
 * telnet to the board has no way to know the board's IP, or whether
 * DHCP ever completed at all vs. the greeting logic itself being
 * broken. link_printed stays a one-shot static local -- the physical
 * PHY link doesn't go down/up again just from toggling DHCP (only the
 * IP-layer administrative state does, via netif_set_down()/_up()), so
 * printing it once per boot is still correct. lease_printed, however,
 * is now app->dhcp_lease_printed, NOT a static local -- see that
 * field's own doc comment in app_context.h for why a static one-shot
 * was a real bug here (never re-arms for a second DHCP negotiation
 * later in the same boot). */
static void trace_dhcp_lease_once(app_context_t *app)
{
    static int link_printed = 0;

    /* Split into two separate one-shot prints (added alongside the
     * original lease-only version, same day, after "no lease" turned
     * out ambiguous between "no cable/PHY link at all" and "link's
     * fine, just no DHCP server reachable") -- link-up is a necessary,
     * strictly earlier milestone than a lease, and reports on a
     * completely different layer (PHY vs. DHCP protocol), so silence
     * on this line specifically points at the cable/switch/PHY, not
     * this port's code. */
    if (!link_printed && enet_lwip_rt1062_is_link_up()) {
        ENET_PRINTF("ENET link up\r\n");
        link_printed = 1;
    }

    if (!app->dhcp_lease_printed && enet_lwip_rt1062_has_ip()) {
        uint8_t ip[4], gw[4];
        enet_lwip_rt1062_get_ip(ip);
        ENET_PRINTF("DHCP lease acquired: %u.%u.%u.%u\r\n",
               ip[0], ip[1], ip[2], ip[3]);

        /* Was updateDIPA()'s `Settings.RabbitGateway[i] = *ptr2;`
         * (from di->dhcp_server) + `Settings.useDHCP = 1;` -- the
         * original's DHCP-success callback, fired at exactly this same
         * "lease just landed" moment. Added 2026-07-20, per explicit
         * report ("gateway IP is not populated in Networking form when
         * successful dhcp") -- app_genie_update_network_strings()'s
         * GENIE_GATEWAYIP_STR write already faithfully shows
         * app->settings.rabbit_gateway (matches the original's own
         * updateNetworkStrings() exactly, no DHCP-vs-static branch
         * there in either version), but nothing in this port ever
         * updated that field from a real DHCP lease -- it only ever
         * held the static-IP default or whatever was last typed via
         * the keypad. */
        enet_lwip_rt1062_get_gateway(gw);
        memcpy(app->settings.rabbit_gateway, gw, 4);
        app->settings.use_dhcp = 1;

        /* Was `if(ifpending(IF_ETH0) == IF_UP){ updateNetworkStrings(); }`
         * -- updateDIPA()'s own tail, right after the settings update
         * above. Pushes a display refresh the moment DHCP succeeds
         * instead of leaving it to whenever the user next happens to
         * (re-)navigate to the Network form. */
#if APP_ENABLE_DISPLAY
        app_genie_update_network_strings(app);
#endif
        app->dhcp_lease_printed = 1;
    }
}
#endif

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
        } else if (ev == TCP_SESSION_NEWLY_CONNECTED) {
            /* TEMPORARY DIAGNOSTIC, added 2026-07-21 per explicit
             * question ("no way you are sending data on the wrong
             * socket instance?") -- tcp_session.c's own greeting-send
             * trace doesn't know its own slot index (tcp_session_process()
             * isn't passed one), so there was no way to directly confirm
             * the greeting and a later chip-read send (which DOES log
             * slot=%d) are even the same connection. This closes that
             * gap. Remove once that report is resolved. */
            ENET_PRINTF("TCP slot %d: NEWLY_CONNECTED\r\n", i);
            /* Was `genieWriteObject(GENIE_OBJ_LED, GENIE_LED_LSOC, 1);`
             * -- ProcessDataSocket()'s ClientIsConnected==0 branch,
             * right after the greeting is sent (ACTIVERFID_V1.02_UHF.c
             * line 2429). tcp_session_process() (tcp_session.c) already
             * correctly detects this exact moment and returns
             * TCP_SESSION_NEWLY_CONNECTED for it -- the greeting/status
             * messages it also sends there were already confirmed
             * working; this event just had no handler here to react to
             * it. display_set_led()/DISPLAY_LED_LOCAL_SOCKET already
             * existed and correctly maps to GENIE_LED_LSOC
             * (display_stub.c) -- just never had a call site. Added
             * 2026-07-20, per explicit report ("LSOC button on lcd not
             * lit when socket is connected"). */
#if APP_ENABLE_DISPLAY
            display_set_led(DISPLAY_LED_LOCAL_SOCKET, 1);
#endif
        } else if (ev == TCP_SESSION_DISCONNECTED) {
            /* TEMPORARY DIAGNOSTIC, added 2026-07-21 -- see the matching
             * NEWLY_CONNECTED trace above for why. Remove once that
             * report is resolved. */
            ENET_PRINTF("TCP slot %d: DISCONNECTED\r\n", i);
            /* Turn-off-on-disconnect, added 2026-07-20 per explicit
             * request ("I want the LED off when socket closes. The
             * rabbitcore was quirky with this but we had something
             * that kind of worked"). The original's own three attempts
             * at this (lines 2406/2437/3039/3091, ACTIVERFID_V1.02_UHF.c)
             * are all commented out, one with the author's own "does
             * not work!!!" -- a real Rabbit-side quirk (likely
             * `tcp_tick()`/`sock_alive()` polling-timing related, per
             * that comment's placement), not something inherent to the
             * underlying task. This port's lwIP-based
             * tcp_session_process() already cleanly detects this exact
             * transition (TCP_SESSION_DISCONNECTED, set right where
             * `session->client_connected` flips 1->0) without any of
             * the original's polling quirks, so implementing this
             * properly here rather than reproducing the original's
             * broken attempt. Only turns the LED off once NO client
             * slot is connected -- with TCP_LWIP_MAX_CLIENTS>1 possible,
             * one client disconnecting shouldn't dim the LED while
             * another is still live. */
#if APP_ENABLE_DISPLAY
            {
                int any_still_connected = 0;
                int s;
                for (s = 0; s < TCP_LWIP_MAX_CLIENTS; s++) {
                    if (app->tcp_listener.sessions[s].client_connected) {
                        any_still_connected = 1;
                        break;
                    }
                }
                if (!any_still_connected) {
                    display_set_led(DISPLAY_LED_LOCAL_SOCKET, 0);
                }
            }
#endif
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
     * subtraction, same underflow-prone class of bug the original's
     * beeper timing (`MS_TIMER - BEEPDELAY > LastBeepTime`) also had.
     * Using ms_elapsed() here for the same reason: fixed rather than
     * reproduced, since there's no reason to keep a bug that only
     * bites in a narrow window after boot/rollover. */
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
/* Section: continuous chip-read buzzer timeout                         */
/* ------------------------------------------------------------------ */

/* REPLACES the old app_beep()/app_beep_n()/buzzer_phase pulse-train
 * machinery (removed 2026-07-17) -- that mechanism modeled a fixed-
 * count queue of discrete pulses (e.g. "two beeps for start reading"),
 * but its ONLY remaining live caller by this point was the tag-read
 * case below, which never actually used the queuing/multi-pulse half
 * of it (the start/stop-reading beeps had already moved to
 * buzzer_beep_n_blocking() earlier the same session -- see
 * app_pc_dispatch.c) -- just the "re-arm an on-timer on every read so
 * a continuous run sounds solid" half. Per explicit report ("I want a
 * fairly solid constant buzzer when there are this many chips being
 * continuously read... timeout where the buzzer stops if not one chip
 * is read after say 200ms"), this is now a dedicated, simpler
 * mechanism with no queue/gap-phase concept at all: on a read, turn
 * on (if not already on) and stamp the time; each iteration, turn off
 * once UHF_READ_BUZZER_TIMEOUT_MS has passed with no further read. */
static void process_uhf_read_buzzer(app_context_t *app, uint32_t now_ms)
{
    if (app->uhf_read_buzzer_on
        && ms_has_elapsed(now_ms, app->uhf_last_read_ms, UHF_READ_BUZZER_TIMEOUT_MS)) {
        buzzer_off();
        app->uhf_read_buzzer_on = 0;
    }
}

/* Same shape as process_uhf_read_buzzer() above, for Active/LF (nRF)
 * chip reads -- see app_context.h's nrf_read_buzzer_on/nrf_last_read_ms
 * field comment for why this is separate state rather than reusing the
 * UHF pair. */
static void process_nrf_read_buzzer(app_context_t *app, uint32_t now_ms)
{
    if (app->nrf_read_buzzer_on
        && ms_has_elapsed(now_ms, app->nrf_last_read_ms, UHF_READ_BUZZER_TIMEOUT_MS)) {
        buzzer_off();
        app->nrf_read_buzzer_on = 0;
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

        /* TRACE, 2026-07-15 -- prints every dequeued touch event's raw
         * object/index/data BEFORE dispatch, so touching a widget on the
         * physical screen gives immediate, visible confirmation of what
         * was recognized -- this is the actual goal of this bring-up
         * pass (verify app_genie_dispatch.c's routing against real
         * touches, one widget at a time, with everything else in
         * bringup_config.h deliberately off). Decodes the object type
         * name for the widget classes app_dispatch_genie_event() itself
         * switches on; unrecognized object values just print numeric. */
        {
            const char *obj_name;
            switch (ev.object) {
            case GENIE_OBJ_FORM:       obj_name = "FORM";       break;
            case GENIE_OBJ_USERBUTTON: obj_name = "USERBUTTON"; break;
            case GENIE_OBJ_KNOB:       obj_name = "KNOB";       break;
            case GENIE_OBJ_TRACKBAR:   obj_name = "TRACKBAR";   break;
            case GENIE_OBJ_4DBUTTON:   obj_name = "4DBUTTON";   break;
            case GENIE_OBJ_SLIDER:     obj_name = "SLIDER";     break;
            case GENIE_OBJ_WINBUTTON:  obj_name = "WINBUTTON";  break;
            case GENIE_OBJ_KEYBOARD:   obj_name = "KEYBOARD/KEYPAD"; break;
            default:                   obj_name = "OTHER";      break;
            }
            PRINTF("Genie event: object=%s(%d) index=%d data=%d\r\n",
                   obj_name, ev.object, ev.index, ev.data);
        }

        /* Was myGenieEventHandler() -- the full touch-event dispatcher
         * (forms, knob/trackbar/slider/4Dbutton/winbutton, keyboard/
         * keypad text entry). Its GENIE_OBJ_4DBUTTON case handles
         * GENIE_SYSTEM the same way this loop used to inline (calling
         * app_uhf_active_mode_toggle()), now correctly nested under an
         * object==GENIE_OBJ_4DBUTTON check matching the real original
         * structure -- the inline version this replaced checked
         * `ev.index == GENIE_SYSTEM` alone, which could misfire on any
         * other widget's event that also happened to carry index 0
         * (e.g. GENIE_FORM_SPLASHSCREEN). */
        app_dispatch_genie_event(app, &ev);
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
                display_set_digits(GENIE_DLED_HOUR, gps_time.hour);
                display_set_digits(GENIE_DLED_MIN, gps_time.min);
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
            int was_synced = app->ds_rollover_seen;

            app->ds_last_edge_ms = edge_ms;
            /* Was `ds_rollover = 1` in the ISR -- sticky from here on,
             * matching the original never resetting it back to 0. Gates
             * UHF tag processing (see process_uhf_reading()). */
            app->ds_rollover_seen = 1;

            /* Button LED, added 2026-07-17 per explicit test request
             * ("red lit on bootup, green when we have time sync, green
             * flashing for reader reading"). This whole block only ever
             * runs from the moment of the FIRST real 1Hz tick onward
             * (ds3231_rt1062_poll_rollover() returning true IS that
             * tick), so `was_synced` captured above is false on exactly
             * one pass -- the first -- letting this fire the red->green
             * transition exactly once, the moment time sync is first
             * achieved.
             *
             * The blink half was `if(ProgramState==READING){
             * BitWrPortI(PBDR,&PBDRShadow,toggle,6); toggle^=1; }`
             * (ACTIVERFID_V1.02_UHF.c lines 3605-3607) -- the ONLY place
             * in the original that touches this LED once per second,
             * always the first thing done inside the `if(ds_rollover==1)`
             * block, i.e. this exact per-tick edge. Writes the CURRENT
             * blink phase to the pin, THEN flips it for next tick --
             * matching the original's exact write-then-toggle order.
             * GREEN/off (not the original's implicit single color) per
             * the same explicit test request. */
            if (app->program_state == APP_STATE_READING) {
                if (app->button_led_blink_state) {
                    button_led_green();
                } else {
                    button_led_off();
                }
                app->button_led_blink_state ^= 1;
            } else if (!was_synced) {
                button_led_green();
            }

            if (ds3231_rt1062_read(&app->current_time) == 0) {
                time_display_actions_t actions =
                    time_sync_display_refresh_actions(&app->current_time);
#if APP_ENABLE_DISPLAY
                if (actions.refresh_seconds) {
                    /* TEMPORARY DIAGNOSTIC, added 2026-07-16 -- REMOVE
                     * once confirmed. Per explicit report that the
                     * seconds digit doesn't tick immediately when MAIN
                     * opens -- this write uses the same
                     * genie_write_object() ACK-wait mechanism already
                     * known (this session) to sometimes time out, and
                     * a failure here was previously completely silent.
                     * If this fires repeatedly right after MAIN
                     * activates, that's direct evidence the seconds
                     * digit is genuinely missing ticks (not just a
                     * perception/expectation mismatch), and confirms
                     * where to add a retry. */
                    if (display_set_digits(GENIE_DLED_SEC, app->current_time.sec) != 1) {
                        PRINTF("Display: seconds digit write did not ACK (sec=%d)\r\n", app->current_time.sec);
                    }
                }
                if (actions.refresh_minutes) {
                    display_set_digits(GENIE_DLED_MIN, app->current_time.min);
                }
                if (actions.refresh_hours) {
                    display_set_digits(GENIE_DLED_HOUR, app->current_time.hour);
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

        /* TEMPORARY DIAGNOSTIC, added 2026-07-17 per explicit report
         * ("getting no chip reads") -- confirms whether the parser is
         * even finding tag-read frames in what process_uhf_reading()
         * reads off the wire, and separately flags the ds_rollover_seen
         * gate below as the reason a genuinely-parsed frame might still
         * never reach the chip array/display/beep. Remove once reads
         * are confirmed flowing end to end. */
        PRINTF("UHF poll: TAG_READ chip=0x%08lX ant=%d rssi=%d%s\r\n",
               (unsigned long)ev->data.tag.chip_code, ev->data.tag.antenna,
               ev->data.tag.rssi,
               app->ds_rollover_seen ? "" : " -- DROPPED (ds_rollover_seen not set yet)");

        /* GATE, was `if(ds_rollover) TM_ProcessChip(p, NoChrs-i);` --
         * TM_ProcessChip is where the original does BOTH the chip-array
         * add AND (inside the same function) the Beeper-gated Beep()
         * call, all wrapped in this one outer check. Before the DS3231's
         * first 1Hz tick since boot, app->current_time is whatever the
         * struct was zero/default-initialized to, so a read logged here
         * would carry a garbage epoch timestamp -- drop the read
         * entirely instead, matching the original exactly (it isn't
         * buffered for later, it's just ignored). ds_rollover_seen is
         * sticky (set once in process_time_sync() and never cleared),
         * so this only actually filters anything in the narrow window
         * between boot and the first RTC tick. */
        if (!app->ds_rollover_seen) {
            break;
        }

        {
            uhf_chip_add_result_t r = uhf_chip_array_add(
                app->uhf_chips, UHF_CHIP_ARRAY_SIZE,
                ev->data.tag.chip_code, ev->data.tag.rssi, ev->data.tag.antenna,
                (uint32_t)rtc_datetime_to_epoch(&app->current_time), app->ds_last_edge_ms,
                0 /* fix_rssi_update_bug -- CHANGED 2026-07-17, was 1.
                     CONFIRMED from source this call site was NOT
                     matching the original: TM_AddToChipArray()'s
                     iOverwriteChip is computed (Chips[i].RSSI < RSSI on
                     a re-read) but the field-update block that would
                     act on it is gated `!iChipFound`, which is false
                     whenever iOverwriteChip is true -- so on real
                     hardware, a chip re-read NEVER updates RSSI/
                     antenna/Seconds/MilliSeconds, only Reads++. The
                     entry's timestamp is frozen at its FIRST read and
                     ages out (see UHF_CHIP_AGING_SECONDS) exactly 3s
                     after that first read, regardless of how many times
                     it gets re-read in between. 0 matches this exactly;
                     1 (the previous value here) was an unrequested
                     "fix" of what's actually the original's real
                     behavior -- see uhf_chip_array.h's own doc comment
                     on this flag, which already described both paths
                     correctly, just defaulted to the wrong one at this
                     call site. */,
                &app->uhf_chip_count, &app->uhf_unique_chips, &idx);
            (void)r;
        }

        /* GAP FOUND AND FIXED 2026-07-17, per explicit question ("are
         * you updating the screen with the latest chip read?") -- was
         * `if (chipcode_display != iLastChip){ ...genieWriteStr(...);
         * chipcode_display = iLastChip; }` at the end of TM_ProcessChip()
         * (UHF_READER.LIB) -- writes the newly-read chip code to
         * GENIE_TXPDR_STR (the same widget the Active/LF-mode path
         * already uses via app->last_lf_code), formatted per
         * Settings.OutputType (DEC/HEX), but only when the code actually
         * CHANGES from the last one shown -- avoids re-writing the
         * display on every single repeat read of the same tag. This had
         * no port anywhere in this codebase until now -- the display
         * simply never showed live UHF read data. */
#if APP_ENABLE_DISPLAY
        if (app->chipcode_display != ev->data.tag.chip_code) {
            char code[16];
            if ((output_type_t)app->settings.output_type == OUTPUT_HEX) {
                snprintf(code, sizeof(code), "%lX", (unsigned long)ev->data.tag.chip_code);
            } else {
                snprintf(code, sizeof(code), "%lu", (unsigned long)ev->data.tag.chip_code);
            }
            display_set_string(GENIE_TXPDR_STR, code);
            app->chipcode_display = ev->data.tag.chip_code;
            /* genie_write_str() (behind display_set_string()) can block
             * for up to g->genie_cmd_timeout (1250ms!) waiting for the
             * display's ACK -- found 2026-07-17 to be a real, likely
             * dominant contributor to "still getting buzzer dropout on
             * constant read of tags" (this branch fires on every
             * DIFFERENT chip code shown, which is often during a real
             * multi-tag stream). Not patched here individually -- see
             * process_uhf_reading()'s own general fix for this whole
             * class of "blocking work started BY a read shouldn't count
             * as that read having stopped" problem, which covers this
             * call along with the nand_log flush and any future
             * blocking call added to this same dispatch path. */
        }
#endif

        /* Beep on the READ itself, not on the record later being
         * logged/flushed. REWRITTEN 2026-07-17 (see
         * process_uhf_read_buzzer()'s own doc comment for the full
         * story) -- only turns the buzzer on here if it isn't already
         * on, then just re-stamps the timestamp on every subsequent
         * read; process_uhf_read_buzzer() (called once per main-loop
         * iteration) is what actually turns it off, after
         * UHF_READ_BUZZER_TIMEOUT_MS with no further read. A rapid run
         * of reads (many chips in range at once) keeps re-stamping the
         * timestamp before that timeout can fire, so the buzzer holds
         * solidly on for as long as reads keep coming in -- not a
         * stutter of individual pulses. */
        if (app->settings.beeper) {
            app->uhf_last_read_ms = c->now_ms;
            if (!app->uhf_read_buzzer_on) {
                app->uhf_read_buzzer_on = 1;
                buzzer_on();
            }
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
    /* TEMPORARY DIAGNOSTIC, added 2026-07-17, same reason as
     * UHF_FRAME_TAG_READ's trace above -- confirms whether the reader
     * is sending ANYTHING recognizable at all (these frame types are
     * far more likely to show up than an actual tag read if there's
     * genuinely no tag in range) versus total silence, which would
     * point at the transport/wiring instead of "no tag present." */
    case UHF_FRAME_END_OF_ROUND:
        PRINTF("UHF poll: END_OF_ROUND frame\r\n");
        break;
    case UHF_FRAME_HEARTBEAT:
        PRINTF("UHF poll: HEARTBEAT frame\r\n");
        break;
    case UHF_FRAME_START_CONFIRM:
        PRINTF("UHF poll: START_CONFIRM frame\r\n");
        break;
    case UHF_FRAME_TEMPERATURE:
    default:
        break;
    }
}

static void process_uhf_reading(app_context_t *app, uint32_t now_ms)
{
    /* Set when this call's transport read actually returned bytes --
     * used at the very end of this function (see the comment there) to
     * decide whether to refresh the buzzer's activity timestamp AFTER
     * all of this call's own processing (parsing, display writes,
     * nand_log flush) has finished, however long that took. */
    int had_bytes = 0;

    if (!app->uhf_reading) {
        return;
    }

    {
        /* Reassembly across reads, added 2026-07-17 per explicit
         * report: a UHF frame can legitimately straddle two separate
         * transport reads (e.g. a read's 3ms timeout elapses partway
         * through a frame still arriving byte-by-byte over the UART).
         * uhf_process_buffer() already reports exactly how much of its
         * input it actually consumed (its return value) versus left
         * unconsumed because a frame was declared longer than the
         * bytes available -- previously that unconsumed tail was
         * simply discarded when `buf` (a stack-local array) went out
         * of scope at the end of this block, silently losing the part
         * message and any chance of ever completing it. Fixed by
         * carrying it forward in app->uhf_carry_buf/uhf_carry_len and
         * prepending it to the front of the buffer before each new
         * read, so a torn frame gets a chance to complete once its
         * remaining bytes arrive, instead of being lost and having the
         * parser resync mid-frame (which -- since the leftover always
         * starts exactly at a sync byte, never mid-frame -- previously
         * risked being misread as garbage or a different frame
         * entirely on the next pass).
         *
         * buf's total size (1024) comfortably covers the carry-over's
         * own max size (512, see app_context.h's field comment) plus
         * a large fresh read on top -- the carry-over can never be
         * more than one incomplete frame's worth (at most 262 bytes,
         * uhf_process_buffer() never leaves more than that unconsumed
         * at the tail), so this never gets tight in practice. */
        uint8_t buf[1024];
        size_t carried = app->uhf_carry_len;
        int n;

        if (carried > 0) {
            memcpy(buf, app->uhf_carry_buf, carried);
        }

        n = app->uhf_transport.read(app->uhf_transport.ctx, buf + carried,
                                     sizeof(buf) - carried, 3);
        if (n > 0) {
            size_t total = carried + (size_t)n;
            size_t consumed;
            size_t leftover;
            uhf_event_ctx_t ctx = { app, now_ms };

            had_bytes = 1;

            /* TEMPORARY DIAGNOSTIC, added 2026-07-17 per explicit report
             * ("getting no chip reads") -- confirms the transport is
             * receiving ANYTHING at all from the reader during active
             * inventory. Zero of these ever printing (with uhf_reading
             * confirmed true) points at the transport/wiring rather
             * than "no tag in range." Remove once reads are confirmed
             * flowing end to end. */
            PRINTF("UHF poll: %d bytes from transport (%u carried over)\r\n",
                   n, (unsigned)carried);

            consumed = uhf_process_buffer(buf, total, uhf_event_cb, &ctx);

            leftover = total - consumed;
            if (leftover > sizeof(app->uhf_carry_buf)) {
                /* Should not happen -- see app_context.h's field
                 * comment on the sizing margin. If it ever does, this
                 * is genuinely unresynced data ballooning without
                 * bound, not a real torn frame -- drop it rather than
                 * overflow, matching uhf_process_buffer()'s own
                 * "resync by discarding" philosophy for unrecognized
                 * bytes elsewhere. */
                PRINTF("UHF poll: carry-over would exceed buffer (%u bytes) -- dropping\r\n",
                       (unsigned)leftover);
                leftover = 0;
            } else if (leftover > 0) {
                memmove(app->uhf_carry_buf, buf + consumed, leftover);
                PRINTF("UHF poll: %u bytes carried to next read (incomplete frame)\r\n",
                       (unsigned)leftover);
            }
            app->uhf_carry_len = leftover;
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

            /* GAP FOUND AND FIXED 2026-07-17, per explicit confirmation
             * -- was `genieWriteObject(GENIE_OBJ_LED_DIGITS,
             * GENIE_DLED_READS, chip_reads)` in TM_ProcessChipArray()'s
             * per-record loop (UHF_READER.LIB), called once per
             * newly-logged record with the running total. Never ported
             * -- GENIE_DLED_READS was only ever written on the
             * clear-counter button (GENIE_BUTTON_RESET), never updated
             * as new reads actually landed. Written ONCE here with the
             * final post-batch total instead of once per record inside
             * the loop below: same end-visible value (a human can't see
             * the intermediate per-record values from a tight loop
             * anyway), but avoids up to `n` extra Genie writes per
             * flush -- each carries real serial-protocol ACK-wait
             * latency (see genie_display.c), so doing this once per
             * batch instead of once per record is a real efficiency
             * win with no observable behavior difference. */
#if APP_ENABLE_DISPLAY
            display_set_digits(GENIE_DLED_READS, app->chip_reads);
#endif

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

                /* Was `Settings.iLastTimeSent = logEntry.date_time;`,
                 * WriteChipToSocket()'s own tail (line 667) -- runs on
                 * every call regardless of record type, so this needs
                 * replicating at both this UHF call site and
                 * process_nrf_spi()'s Active/LF one (this port has no
                 * single shared WriteChipToSocket()-equivalent function
                 * the way the original does). Found missing 2026-07-20:
                 * `last_time_sent` was never assigned ANYWHERE in this
                 * file, only ever read (DATA form's "Last Read" display,
                 * and PC_CMD_START_LIVE_DATA's default resume point when
                 * the client sends from_time=0) -- so both of those
                 * silently used a stale/default value forever. No
                 * SaveSettings()/app_persist_settings() call here either,
                 * matching the original -- this is a best-effort, RAM-only
                 * update, not aggressively flushed to flash on every read. */
                app->settings.last_time_sent = records[i].date_time;

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

            /* Per-record date_time trace, added 2026-07-20 per explicit
             * report ("timestamp date is wrong (19/07/2036) but time
             * like right" -- clarified to be UHF reader data, not nRF).
             * UHF records get date_time from
             * `rtc_datetime_to_epoch(&app->current_time)` at the moment
             * of the read (see uhf_event_cb()'s UHF_FRAME_TAG_READ case
             * above), i.e. the SAME app->current_time the Genie MAIN
             * screen displays -- if this raw value decodes to the wrong
             * year/date, either app->current_time itself was wrong at
             * read time (a GPS-sync or RTC issue, not UHF-specific), or
             * something downstream of this trace point (CSV formatting,
             * or the PC client's own decode) is misinterpreting an
             * otherwise-correct value. */
            {
                size_t di;
                for (di = 0; di < n; di++) {
                    PRINTF("UHF flush record[%u]: date_time=%lu\r\n",
                           (unsigned)di, (unsigned long)records[di].date_time);
                }
            }

            /* TEMPORARY DIAGNOSTIC, added 2026-07-17 per explicit report
             * ("reading stalls after about 10 seconds") -- this is the
             * first time real UHF traffic has ever driven repeated
             * littlefs writes to the SAME QSPI flash the firmware
             * executes from, on a roughly-every-3-seconds cadence
             * (UHF_CHIP_AGING_SECONDS) for as long as reads keep
             * arriving. mflash_drv.c's erase/program routines disable
             * global interrupts (PRIMASK) for their duration, same as
             * any XIP flash write must -- while that's happening,
             * LPUART8's RX ISR can't run, so any reader bytes arriving
             * during a flash program/erase are lost at the hardware
             * level, not just dropped from the ring buffer. A ~10s
             * stall lines up with a few of these write cycles having
             * already happened. Traces whether the stall coincides with
             * a flush being attempted, and whether the flush itself
             * ever returns (vs hanging inside littlefs/mflash) -- rc
             * values, not just "it happened." Timing added 2026-07-17
             * per a SEPARATE follow-up report ("still getting buzzer
             * dropout on constant read of tags") -- measures how long
             * this blocking call actually takes on real hardware; see
             * this function's own general fix (right at the end) for
             * why this duration no longer costs the buzzer anything,
             * this line just gives real numbers to confirm it. Remove
             * once confirmed / no longer needed. */
            {
                int rc_open, rc_append, rc_close;
                uint32_t flush_start_ms = systick_ms_now();
                uint32_t flush_end_ms;
                PRINTF("UHF poll: flushing %u aged record%s to nand_log\r\n",
                       (unsigned)n, n == 1 ? "" : "s");
                rc_open = nand_log_open_for_append(&app->log);
                rc_append = nand_log_append_records(&app->log, records, n);
                rc_close = nand_log_close(&app->log);
                flush_end_ms = systick_ms_now();
                PRINTF("UHF poll: nand_log flush done in %u ms, open=%d append=%d close=%d\r\n",
                       (unsigned)ms_elapsed(flush_end_ms, flush_start_ms), rc_open, rc_append, rc_close);
            }
        }
    }

    /* GENERAL FIX, added 2026-07-17, root cause of "still getting
     * buzzer dropout on constant read of tags" even after the
     * UHF_READ_BUZZER_TIMEOUT_MS correction and an earlier point-fix
     * for the nand_log flush specifically: this WHOLE function can do
     * real blocking work as a side effect of processing a read --
     * genie_write_str() behind the GENIE_TXPDR_STR display update
     * above can block up to 1250ms waiting for the display's ACK, and
     * the nand_log flush above can take an unknown/unbounded amount of
     * time on a real flash erase. EITHER can individually exceed
     * UHF_READ_BUZZER_TIMEOUT_MS (300ms), making process_uhf_read_buzzer()
     * think no read has happened "recently" and turn the buzzer off,
     * even though real tag reads never actually stopped arriving -- we
     * were just still busy handling the consequences of the last one.
     * Rather than keep patching each individual blocking call site
     * (already done once for the display write, once for the flush,
     * both superseded by this), this refreshes uhf_last_read_ms ONCE
     * here, using a fresh systick_ms_now() reading taken AFTER every
     * bit of this call's own processing has finished, however long
     * that took -- covering both known blocking calls above and any
     * future one added to this same function. Gated on had_bytes (a
     * real transport read actually returned something this call, not
     * just "the buzzer happened to already be on") so a call that
     * genuinely saw nothing from the reader still lets the timeout
     * elapse normally -- this is about not penalizing a slow BUT
     * REAL read, not about propping the buzzer up indefinitely once
     * the reader genuinely goes quiet. Also gated on uhf_read_buzzer_on
     * (already on) -- a read arriving with the buzzer not yet on is
     * handled by uhf_event_cb() turning it on directly; this is purely
     * about not letting it turn off prematurely once it already is. */
    if (had_bytes && app->uhf_read_buzzer_on) {
        app->uhf_last_read_ms = systick_ms_now();
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

/* Was ACTIVERFID_V1.02_UHF.c lines 3732-3742 (the checkInterval
 * battery block) -- the old "MP2731/charger library not provided" TODO
 * that used to sit here was stale by the time this was found
 * 2026-07-16: max17303_fuel_gauge_rt1062.c/h already exists and is
 * confirmed working (see bms_init.c's board-version read + project
 * memory). Faithfully ported: MAX17303_REG_REPSOC is a 1/256-%-per-LSB
 * raw value; original's own "+1, never seems to get to 100" fudge and
 * 100% cap both preserved verbatim. Gated on board_vers>=32 exactly
 * like the original -- app->board_version only ever gets set away from
 * its 0 default by bms_init()'s real 0x4067 signature check, so this
 * stays a correct no-op (matching original behavior for boards below
 * that revision) until APP_ENABLE_BMS is on.
 * RESTORED 2026-07-20 -- the original ALSO gates this whole block
 * behind `!BitRdPortI(PBDR,3)` (PB3 = NRF_READY input, GPIO3 pin 2 in
 * this port). Previously skipped as "genuinely unclear... real bus-
 * safety interlock or just a cooperative-scheduling artifact" -- now
 * fairly confident it's the latter (scheduling priority, not hardware
 * safety): the original gates FOUR unrelated call sites behind this
 * exact same bit (this I2C battery read, the GPS SPI signal check, and
 * two nRF time-push call sites), including a plain I2C read that has
 * no electrical relationship to the nRF's SPI link at all -- only a
 * "don't let slow work delay draining a pending nRF read" scheduling
 * rule explains gating an unrelated bus like that. Matches the
 * already-confirmed-safe 0xBB-sentinel tolerance in nrf_spi_poll() --
 * this is priority, not corruption prevention.
 * Guarded on APP_ENABLE_NRF_SPI (not just a runtime check) for the same
 * reason the original comment already gave: NRF_READY's GPIO is only
 * ever configured inside nrf_spi_transport_rt1062_init(), so reading it
 * with that stage off would read unconfigured pin state -- treated as
 * "not busy" in that case so this stays a correct no-op-safe skip
 * rather than trusting garbage, consistent with the rest of this port's
 * staged-flag philosophy.
 * EXTRACTED into its own function 2026-07-16 (was inline in
 * process_periodic_checks() below) so app_init() can also call it once
 * immediately before the first display paint -- otherwise the Genie
 * battery gauge/digits show a stale 0% for the first
 * BATTERY_CHECK_MS/2000ms of every boot, same class of bug as the
 * DS3231-before-display ordering issue fixed earlier this session.
 * Returns 1 if it actually ran (nRF not busy), 0 if skipped -- lets
 * process_periodic_checks() below replicate the original's exact
 * nesting, where checkInterval is only advanced on the same pass the
 * read actually happened (a busy nRF just retries next loop pass
 * instead of waiting a full BATTERY_CHECK_MS). app_init()'s one-shot
 * boot-time call ignores the return value -- nothing to reschedule
 * there. */
int app_update_battery_percent(app_context_t *app)
{
#if APP_ENABLE_NRF_SPI
    if (app->nrf_transport.read_ready_line(app->nrf_transport.hw_ctx)) {
        return 0; /* nRF has a pending read queued -- defer, matches original */
    }
#endif
#if APP_ENABLE_BMS
    if (app->board_version >= 32) {
        uint16_t max_register;
        if (max17303_read_reg(MAX17303_ADDR_MAIN, MAX17303_REG_REPSOC, &max_register) == 0) {
            float max_value = (float)max_register * (1.0f / 256.0f);
            max_value += 1.0f; /* "never seems to get to 100 so top it up 1" -- original's own comment */
            if (max_value > 100.0f) {
                max_value = 100.0f;
            }
            app->batt_percent = (int)max_value;
        }
    }
#else
    (void)app;
#endif
    return 1;
}

static void process_periodic_checks(app_context_t *app, uint32_t now_ms)
{
    if (now_ms > app->check_interval_ms) {
        if (app_update_battery_percent(app)) {
            app->check_interval_ms = now_ms + BATTERY_CHECK_MS;
        }
        /* else: nRF busy, retry next pass -- was the original's
         * `if(!BitRdPortI(PBDR,3)){ ...; checkInterval = MS_TIMER + DELAY; }`,
         * where checkInterval only advances on the same pass the read
         * itself ran. */
    }

    /* Was `if(set_time){ if(...checkInterval2...){ GetGPS_Signal(); }}`
     * -- the original gates its own satellite-count/fix-status readout
     * behind time already having been set, which is circular for
     * bring-up diagnostics (can't see whether the module has a fix to
     * explain why time ISN'T set yet). Deliberately decoupled from
     * app->gps_time_set here 2026-07-16, per explicit request: this now
     * runs on its own timer regardless of app->gps_time_set, gated only
     * on APP_ENABLE_GPS (not a runtime check -- gps_update_signal_status()
     * lazily inits the GPS SPI transport on first call, which shouldn't
     * happen at all if this stage is compiled out). Also added a
     * debug-UART trace of the raw satellite count/fix status (neither
     * was observable anywhere before -- the Genie tank widget only
     * shows a scaled 0-100 bar, gated behind APP_ENABLE_DISPLAY, and
     * even then requires physically reading a bar's height).
     * RESTORED 2026-07-20 -- the original's own `!BitRdPortI(PBDR,3)`
     * gate around GetGPS_Signal() itself (distinct from the set_time
     * wrapper decoupled above, which stays decoupled per that explicit
     * instruction) -- see app_update_battery_percent()'s doc comment
     * for why this is believed to be scheduling priority, not a bus
     * safety requirement. Same nesting as the original: checkInterval2
     * only advances on the pass the check actually ran. */
#if APP_ENABLE_GPS
    if (now_ms > app->check_interval2_ms) {
#if APP_ENABLE_NRF_SPI
        int nrf_busy = app->nrf_transport.read_ready_line(app->nrf_transport.hw_ctx);
#else
        int nrf_busy = 0;
#endif
        if (!nrf_busy) {
            int raw_sats = -1, status = -1;
            gps_update_signal_status(&raw_sats, &status);
            PRINTF("GPS signal: %d sats, status=%d, pps=%d\r\n", raw_sats, status, app->pps);
            app->check_interval2_ms = now_ms + GPS_SIGNAL_CHECK_MS;
        }
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Section: rewind-while-reading                                        */
/* was `costate { wfd RewindLogFile_BinarySearch(8); abort; }`          */
/*                                                                        */
/* REWRITTEN 2026-07-20 as a resumable state machine, per explicit      */
/* requirement ("a rewind needs to be able to happen whilst new records */
/* are being appended to the file... needs to be unblocking"). The      */
/* original is a `cofunc` that `yield`s after every single record --    */
/* this port's previous version instead did the whole binary-search-   */
/* then-stream-to-completion sequence in ONE blocking call, which had   */
/* two real problems: (1) a large rewind would freeze the entire main   */
/* loop (touch/GPS/nRF polling/live reads all stall) for however long   */
/* it took, and (2) it used `nand_log_open_for_read()`, sharing the     */
/* SAME `file`/`open` slot as `nand_log_open_for_append()` -- so a      */
/* rewind and a live-record append genuinely could not coexist even in  */
/* principle, whichever tried to open second just failed outright.      */
/*                                                                        */
/* Fixed both: rewind now uses a SEPARATE dedicated handle              */
/* (`nand_log_rewind_*`, see nand_log_littlefs.h/.c) that stays open     */
/* across many process_rewind() calls without ever touching the append  */
/* handle, and each call processes a small bounded batch                */
/* (REWIND_RECORDS_PER_CALL) before returning control to the rest of    */
/* the main loop -- the non-blocking equivalent of the original's       */
/* per-record `yield`. RWR_STARTING (open + binary search, one-shot)    */
/* and RWR_READING (stream one batch per call) are two distinct states  */
/* now instead of one blocking pass; RWR_STARTING was already declared  */
/* in rwr_state_t but previously unused by any caller.                  */
/*                                                                        */
/* Also fixes two real correctness bugs found comparing against the     */
/* original's full RewindLogFile_BinarySearch(), not just the           */
/* BinarySearch() half already ported correctly elsewhere:              */
/*   1. The per-record range filter/stop check now branches on          */
/*      rewind_type (date_time for REWIND_BY_TIME, log_id for           */
/*      REWIND_BY_RECNO) -- was always comparing against date_time      */
/*      regardless of type, silently breaking any REWIND_BY_RECNO       */
/*      request (rewind_from_time/to_time would hold record numbers,    */
/*      compared against a totally different field/scale).              */
/*   2. last_time_sent is now updated with the original's own           */
/*      max-tracking guard (`if (date_time > iLastTimeSent)`), not an   */
/*      unconditional overwrite -- replaying old records during a       */
/*      rewind must not push "last sent" backward.                      */
/* ------------------------------------------------------------------ */

#define REWIND_RECORDS_PER_CALL 1u

static void process_rewind(app_context_t *app)
{
    if (app->rwr_state == RWR_STARTING) {
        uint32_t record_no;

        PRINTF("Rewind: starting, type=%d from=%lu to=%lu socket=%d only_unsent=%d\r\n",
               (int)app->rewind_type, (unsigned long)app->rewind_from_time,
               (unsigned long)app->rewind_to_time, app->rewind_socket_index,
               app->only_rewind_unsent);

        if (nand_log_rewind_open(&app->log) != 0) {
            PRINTF("Rewind: FAILED, nand_log_rewind_open() error\r\n");
            app->rwr_state = RWR_STOPPED;
            return;
        }
        if (nand_log_rewind_binary_search(&app->log, app->rewind_from_time,
                                           app->rewind_type, &record_no) != 1) {
            PRINTF("Rewind: FAILED, binary search found nothing to rewind from=%lu\r\n",
                   (unsigned long)app->rewind_from_time);
            nand_log_rewind_close(&app->log);
            app->rwr_state = RWR_STOPPED;
            return;
        }
        PRINTF("Rewind: search OK, starting at record_no=%lu\r\n", (unsigned long)record_no);
        app->rwr_state = RWR_READING;
        return; /* start streaming batches on the next call */
    }

    if (app->rwr_state != RWR_READING) {
        /* Not actively rewinding -- clean up a still-open handle if
         * something changed rwr_state without going through
         * PC_CMD_STOP_REWIND's own explicit close (that's the primary
         * cleanup path; this is a second line of defense, not the norm). */
        if (app->log.rewind_open) {
            nand_log_rewind_close(&app->log);
        }
        return;
    }

    {
        unsigned i;
        for (i = 0; i < REWIND_RECORDS_PER_CALL; i++) {
            nrf_record_t rec;
            uint32_t compare_value;
            int in_range;
            int rc = nand_log_rewind_read_next_record(&app->log, &rec);

            if (rc != 1) {
                /* EOF or a genuine read error -- either way, nothing
                 * more to send this rewind. */
                PRINTF("Rewind: done, %s\r\n", rc == 0 ? "EOF" : "read error");
                nand_log_rewind_close(&app->log);
                app->rwr_state = RWR_STOPPED;
                return;
            }

            compare_value = (app->rewind_type == REWIND_BY_RECNO) ? rec.log_id : rec.date_time;

            in_range = (app->rewind_from_time == 0)
                || (compare_value >= app->rewind_from_time
                    && (app->rewind_to_time == 0 || compare_value <= app->rewind_to_time));

            if (in_range) {
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
                        PRINTF("Rewind: sent record log_id=%lu date_time=%lu (%d bytes) to socket %d\r\n",
                               (unsigned long)rec.log_id, (unsigned long)rec.date_time,
                               len, app->rewind_socket_index);
                    } else if (len > 0) {
                        PRINTF("Rewind: record log_id=%lu in range but NOT sent -- socket index %d invalid\r\n",
                               (unsigned long)rec.log_id, app->rewind_socket_index);
                    }
                }

                if (rec.date_time > app->settings.last_time_sent) {
                    app->settings.last_time_sent = rec.date_time;
                }
            } else if (app->rewind_to_time != 0 && compare_value > app->rewind_to_time) {
                PRINTF("Rewind: done, reached end of requested range (compare_value=%lu > to=%lu)\r\n",
                       (unsigned long)compare_value, (unsigned long)app->rewind_to_time);
                nand_log_rewind_close(&app->log);
                app->rwr_state = RWR_STOPPED;
                return;
            }
        }
    }
    /* Batch done, more may remain -- stay in RWR_READING. The next
     * process_rewind() call (next main-loop iteration, after the rest
     * of the loop -- display/GPS/nRF/TCP -- has had a chance to run)
     * continues from exactly here, same open handle and file position. */
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
    /* Edge-triggered trace, added 2026-07-20 per explicit request ("I am
     * not getting a chip read. Should we see a debug printf if NRF_Ready
     * goes hi?") -- independent of what the subsequent 0x01 poll returns
     * (that trace is deliberately silent for the common "0xBB, nothing
     * queued" case, see simple_command()/nrf_spi_poll()). This just
     * confirms the raw GPIO line itself is toggling at all -- if a
     * physical tag read never produces even this line, the problem is
     * upstream of this port's code (nRF firmware/wiring), not in the
     * poll/retrieve dispatch below. static local, not app state -- pure
     * diagnostic, nothing downstream depends on it. */
    {
        static int s_ready_was_high = 0;
        int ready_now = app->nrf_transport.read_ready_line(app->nrf_transport.hw_ctx);
        if (ready_now && !s_ready_was_high) {
            /* PRINTF silenced 2026-07-20, per explicit request ("clean
             * up this shit, I dont want to see nrf printf anymore"). */
        }
        s_ready_was_high = ready_now;
    }

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
            int n = 0;
            int is_live = (app->nrf_pending_record_type == NRF_RECORD_TYPE_LIVE);
            char pb_code[7] = {0};
            uint32_t pb_boots = 0, pb_bt_time = 0;
            uint8_t pb_fw = 0;

            /* FIXED 2026-07-21, per explicit report of a bogus "playback"
             * record appearing during ordinary live-read testing (playback
             * has never been exercised in this port). Was a plain boolean
             * (`is_live ? live : playback`), so ANY record_type other than
             * 1 -- including a garbage/unrecognized value from a bad poll
             * reply -- unconditionally ran the playback retrieve. The
             * original, ACTIVERFID_V1.02_UHF.c comms_NRF() case 0x02, is a
             * real three-way branch: `if(record_type==1){...live...}
             * else{ if(record_type==2){...playback...} }` -- anything that
             * is neither 1 nor 2 matches NEITHER branch, so the original
             * doesn't even issue the retrieve SPI transfer at all. Restored
             * that exactly: only call a retrieve function for a recognized
             * type; n stays 0 (no-op) for anything else. */
            if (app->nrf_pending_record_type == NRF_RECORD_TYPE_LIVE) {
                n = nrf_spi_retrieve_live_records(&app->nrf_transport,
                                                   app->nrf_pending_record_count,
                                                   records, NRF_SPI_MAX_RECORDS);
            } else if (app->nrf_pending_record_type == NRF_RECORD_TYPE_PLAYBACK) {
                n = nrf_spi_retrieve_playback_records(&app->nrf_transport,
                                                        app->nrf_pending_record_count,
                                                        records, NRF_SPI_MAX_RECORDS,
                                                        pb_code, &pb_boots, &pb_bt_time, &pb_fw);
            }
            app->spi_record_state = 1; /* was the original's unconditional
                reset back to the poll phase after every retrieve attempt */

            if (n > 0) {
                int i, s;

                /* Was the special "@code,records,boots,bt_time,fw" header
                 * line sent to every connected client BEFORE the
                 * per-record data, comms_NRF() case 0x02's record_type==2
                 * branch only (ACTIVERFID_V1.02_UHF.c lines 1139-1148,
                 * `sprintf(buf,"@%s,%d,%lu,%lu,%d\n\r",...)`). Live
                 * records (record_type==1) have no such header. */
                if (!is_live) {
                    char header[48];
                    int header_len = snprintf(header, sizeof(header),
                                               "@%.6s,%d,%lu,%lu,%d\r\n",
                                               pb_code, n,
                                               (unsigned long)pb_boots,
                                               (unsigned long)pb_bt_time,
                                               (int)pb_fw);
                    if (header_len > 0) {
                        for (s = 0; s < TCP_LWIP_MAX_CLIENTS; s++) {
                            if (app->tcp_listener.sessions[s].client_connected) {
                                app->tcp_listener.transports[s].send(
                                    app->tcp_listener.transports[s].ctx,
                                    (const uint8_t *)header, (size_t)header_len);
                            }
                        }
                    }
                }

                for (i = 0; i < n; i++) {
                    char line[100];
                    int len;

                    /* iLastLogID/chip_reads are only touched for live
                     * records (comms_NRF() case 0x02, record_type==1,
                     * lines 1087/1102) -- record_type==2 (playback)
                     * records keep LogID==0 (already set by
                     * nrf_spi_retrieve_playback_records()) and leave
                     * chip_reads alone (`//chip_reads++;`, commented out
                     * in the original at line 1162). */
                    if (is_live) {
                        app->last_log_id++;
                        records[i].log_id = app->last_log_id;
                        app->chip_reads = (uint8_t)(app->chip_reads + 1);

#if APP_ENABLE_DISPLAY
                        /* Was `sprintf(percent_str,"%3fV",batt);
                         * genieWriteStr(GENIE_TXPDR_BAT_STR,percent_str);`
                         * and `sprintf(code,"%.6s",...);
                         * genieWriteStr(GENIE_TXPDR_STR,code);` -- live
                         * chip-code/battery-voltage display update per
                         * retrieved record (lines 1078-1085). percent_str
                         * sized generously here (original's char[5] is too
                         * small for its own "%3fV" format -- a latent
                         * buffer overflow in the original, not replicated).
                         * FIXED 2026-07-20 -- the original's floating-point
                         * `%3fV` produced a scrambled string on real
                         * hardware: this port's newlib-nano build has no
                         * float printf support linked in (confirmed by
                         * grepping the whole codebase -- zero other `%f`
                         * usages anywhere, every other numeric display
                         * value here already goes through integer
                         * formatting; this was the one place a float
                         * format slipped through). Reworked to integer-
                         * only arithmetic computing the mathematically
                         * identical value: battery/100 + 1.8 volts ==
                         * (battery + 180) hundredths-of-a-volt exactly,
                         * since battery is already scaled by 100 and
                         * 1.8 == 180/100. Displays as "X.YYV" (2 decimal
                         * places) instead of the original's 6 -- same
                         * value, cleaner format, no float dependency. */
                        {
                            char code_str[7];
                            char batt_str[16];
                            int batt_hundredths = (int)records[i].battery + 180;
                            memcpy(code_str, records[i].xpdr_code, 6);
                            code_str[6] = '\0';
                            snprintf(batt_str, sizeof(batt_str), "%d.%02dV",
                                     batt_hundredths / 100, batt_hundredths % 100);
                            display_set_string(GENIE_TXPDR_STR, code_str);
                            display_set_string(GENIE_TXPDR_BAT_STR, batt_str);
                        }
#endif
                        /* Was `if(Settings.Beeper) Beep();` (line 1113) --
                         * see app_context.h's nrf_read_buzzer_on field
                         * comment for why this reuses the UHF continuous-
                         * read-buzzer SHAPE (hold on, timeout off) rather
                         * than a single blocking pulse. */
                        if (app->settings.beeper) {
                            app->nrf_last_read_ms = systick_ms_now();
                            if (!app->nrf_read_buzzer_on) {
                                app->nrf_read_buzzer_on = 1;
                                buzzer_on();
                            }
                        }
                    }

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
                        for (s = 0; s < TCP_LWIP_MAX_CLIENTS; s++) {
                            if (app->tcp_listener.sessions[s].client_connected) {
                                /* TEMPORARY DIAGNOSTIC, added 2026-07-21 per
                                 * explicit report ("chip is read ok...no
                                 * data on ethernet on the client") -- the
                                 * return value of send() was never checked
                                 * here (or anywhere else this same pattern
                                 * is used). lw_send() (tcp_transport_lwip.c)
                                 * returns 0 if tcp_sndbuf() is 0 (send
                                 * buffer full) or a short count on a
                                 * partial write -- neither case is retried
                                 * anywhere, so a record could be silently,
                                 * permanently dropped right here. This
                                 * traces exactly that. Remove once this
                                 * report is resolved. */
                                /* PRINTF silenced 2026-07-20, per explicit
                                 * request ("clean up this shit, I dont want
                                 * to see nrf printf anymore") -- the
                                 * underlying report itself is now being
                                 * chased via Wireshark (byte-level ground
                                 * truth), which superseded this trace's
                                 * usefulness. sent still captured in case
                                 * it's wanted again. */
                                int sent = app->tcp_listener.transports[s].send(
                                    app->tcp_listener.transports[s].ctx,
                                    (const uint8_t *)line, (size_t)len);
                                (void)sent;
                            }
                        }
                    }

                    /* Was `Settings.iLastTimeSent = logEntry.date_time;`
                     * and the FinishLynx broadcast, both inside
                     * WriteChipToSocket() (line 667/669-677) -- that
                     * function is called for BOTH record_type==1 (live,
                     * line 1112) AND record_type==2 (playback, line
                     * 1161) in the original, so both belong here, outside
                     * the is_live-only block above -- not just for live
                     * records. Same UHF-flush-block gap, see that call
                     * site's own comment for the full "why" on
                     * last_time_sent specifically. */
                    app->settings.last_time_sent = records[i].date_time;
#if APP_ENABLE_TCP
                    {
                        char fl_line[40];
                        int fl_len = finish_lynx_build_split_string(&records[i], 0, fl_line, sizeof(fl_line));
                        if (fl_len > 0) {
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

                if (is_live) {
#if APP_ENABLE_DISPLAY
                    /* Was `genieWriteObject(GENIE_OBJ_LED_DIGITS,
                     * GENIE_DLED_READS, chip_reads)`, written once per
                     * record in the original's loop (line 1088) -- moved
                     * to once per batch here, same convention (and same
                     * reasoning) already established for UHF's
                     * TM_ProcessChipArray() port -- see the
                     * GENIE_DLED_READS comment in the UHF flush block
                     * above. */
                    display_set_digits(GENIE_DLED_READS, app->chip_reads);
#endif
#if APP_ENABLE_STORAGE
                    /* Was OpenNANDLogFile()/SaveRAMToNAND()/
                     * CloseNANDLogFile() -- record_type==1 only. The
                     * record_type==2 (playback/rewind) equivalent calls
                     * are commented out in the original (lines 1165-1167):
                     * these records were already logged once when first
                     * read live, so re-saving them here on every rewind
                     * replay would duplicate them in the log. */
                    nand_log_open_for_append(&app->log);
                    nand_log_append_records(&app->log, records, (size_t)n);
                    nand_log_close(&app->log);
#endif
                }
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
    /* Was entirely missing until 2026-07-14 -- the "board's ENET
     * driver" half tcp_lwip_poll()'s own doc comment says must be
     * pumped separately (drives the actual RX path, ethernetif_input());
     * tcp_lwip_poll() below only drives lwIP's internal timers
     * (sys_check_timeouts()) -- consolidated replacement for the many
     * scattered tcp_tick(NULL) calls in the original. Both are needed
     * every iteration; order between the two doesn't matter.
     *
     * Gated on app->enet_available, added 2026-07-16 -- if
     * enet_lwip_rt1062_init() bailed out early (PHY never responded,
     * see its own header comment), the netif was never added and none
     * of app->tcp_listener/reset_transport/discovery_transport were
     * ever opened; touching any of them here would be operating on
     * uninitialized state, not just a harmless no-op. */
    if (app->enet_available) {
        enet_lwip_rt1062_poll();
        tcp_lwip_poll();
        enet_lwip_rt1062_poll_dhcp_fallback();
        trace_dhcp_lease_once(app);
        process_data_sockets(app, now_ms);
        process_reset_socket(app);
        process_finish_lynx_socket(app, now_ms);
    }
#endif

#if APP_ENABLE_DISPLAY
    display_do_events();

    /* Retries the boot-time display_activate_form(GENIE_FORM_MAIN) call
     * once the display link is confirmed online, added 2026-07-16.
     * app_init()'s own one-shot attempt silently no-ops if the display
     * hadn't been detected yet at that exact moment (missed its narrow
     * ~150ms boot window -- confirmed to happen on a genuine cold
     * power-up without a debug probe attached, where the display starts
     * booting at the same instant as the RT1062 rather than possibly
     * already being warm from a prior probe-attached test/reflash) --
     * without this retry, the screen was staying stuck on splash
     * forever even though display_do_events()'s own ongoing auto-ping
     * (genie_ping(), called every iteration via genie_do_events() above)
     * recovers the link on its own moments later. This is a resilience
     * fix, not a rewrite of the original's own one-shot activate-form
     * design -- same pattern already applied to enet_lwip_rt1062_init()
     * this session (make a real, confirmed boot-timing race recoverable
     * instead of guessing at a bigger fixed delay). */
    /* Gated to a 2000ms cooldown (app->display_main_retry_ms), added
     * 2026-07-16 -- display_activate_form() can time out (up to
     * genie_cmd_timeout==1250ms, see genie_write_object()) without
     * clearing display_detected, so retrying on literally every
     * iteration on failure would risk the same "retry storm blocking
     * the main loop" class of bug already fixed once this session for
     * GPS. Only marks display_main_shown once an actual ACK (return
     * value 1) confirms the command was applied -- an explicit NAK (0)
     * or timeout (-1) leaves it unset so the next cooldown window
     * tries again, instead of the previous version's bug of marking
     * "done" unconditionally after a single attempt regardless of
     * whether it actually succeeded. */
    if (!app->display_main_shown && display_is_online()
        && ms_has_elapsed(now_ms, app->display_main_retry_ms, 2000u)) {
        int ack;
        app->display_main_retry_ms = now_ms;
        PRINTF("Display: link online, activating MAIN now\r\n"); /* TEMPORARY DIAGNOSTIC, added 2026-07-16 -- confirms this retry path actually fires */
        ack = display_activate_form(GENIE_FORM_MAIN);
        PRINTF("Display: activate MAIN result=%d (1=ACK,0=NAK,-1=timeout)\r\n", ack); /* TEMPORARY DIAGNOSTIC */
        if (ack == 1) {
            app_genie_update_main(app);
            app->display_main_shown = 1;
        }
    }
#endif

    process_display_events(app, now_ms); /* drains the display's event
        queue (GENIE_SYSTEM toggle dispatch, dim timeout) -- must run
        after display_do_events() so events are actually available */

    process_uhf_read_buzzer(app, now_ms); /* just GPIO/timer comparisons,
        no hardware dependency gated behind a flag -- safe to always run */

    process_nrf_read_buzzer(app, now_ms); /* same reasoning as above */

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
