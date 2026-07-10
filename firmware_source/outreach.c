/*
 * outreach.c
 *
 * ==========================================================================
 * SCAFFOLD -- not compiled/tested against real hardware/carrier here
 * (needs a real modem + SIM + cell coverage, or a real LAN target, to
 * meaningfully test), same tier as gprs_modem.c/uhf_reader.c: I/O
 * choreography, harder to unit test than the pure protocol pieces it's
 * built from (gprs_response_parser.h, ip_addr_parse.h -- both already
 * tested).
 * ==========================================================================
 *
 * See outreach.h for the full non-blocking state-machine design
 * this implements (rebuilt after being told the original is a Dynamic C
 * cofunc -- yields every tick, never blocks the caller).
 */

#include "outreach.h"
#include "gprs_modem.h"
#include "gprs_response_parser.h"
#include "ip_addr_parse.h"
#include "tcp_transport_lwip.h"
#include "ms_time.h"
#include "display_stub.h"
#include "bringup_config.h"
#include <stdio.h>
#include <string.h>

#define AT_CHECK_TIMEOUT_MS 0u /* non-blocking: one instant poll per call,
    this file's own step_start_ms/now_ms comparison owns the real timeout */

static void at_send(gprs_modem_t *m, const char *cmd)
{
    m->transport->flush_tx(m->transport->ctx);
    m->transport->flush_rx(m->transport->ctx);
    m->transport->write(m->transport->ctx, (const uint8_t *)cmd, strlen(cmd));
}

/*
 * Was one iteration of the original's `while (MS_TIMER - i < X) { if
 * (GPRS_ReadSer(expected) [==1 or !=0]) break; yield; }` loop -- but
 * now genuinely ONE non-blocking check per call (see AT_CHECK_TIMEOUT_MS),
 * not a loop. require_exact_match=1 reproduces a `==1` check
 * (GPRS_RESP_MATCHED only); require_exact_match=0 reproduces a `!=0`
 * check (any non-empty response counts, matching the original's "we
 * don't care what response we get, just as long as we get one"
 * comments). Returns 1 = matched, 0 = still waiting, -1 = this step's
 * overall timeout has elapsed without matching.
 */
static int check_step(gprs_modem_t *m, const char *expected, int require_exact_match,
                       uint32_t step_start_ms, uint32_t now_ms, uint32_t step_timeout_ms)
{
    char buf[256];
    gprs_response_result_t r = gprs_modem_read_response(m, expected, buf, sizeof(buf), AT_CHECK_TIMEOUT_MS);

    if (require_exact_match) {
        if (r == GPRS_RESP_MATCHED) {
            return 1;
        }
    } else {
        if (r != GPRS_RESP_NONE) {
            return 1;
        }
    }

    if (ms_elapsed(now_ms, step_start_ms) >= step_timeout_ms) {
        return -1;
    }
    return 0;
}

/* Was the fall-through failure/cleanup path at the bottom of the
 * original's RemoteType==1 branch -- reached whenever a modem step
 * fails or times out. These assignments happen up front, matching the
 * original's exact ordering (before the QICLOSE send). */
static void begin_cleanup(app_context_t *app, uint32_t now_ms)
{
    gprs_modem_t *m = &app->modem;

    app->settings.remote_type = 2; /* fall back to LAN next time */
    m->gprs_state = GPRS_STATE_NOGPRS;

    m->transport->flush_rx(m->transport->ctx);
    m->transport->write(m->transport->ctx, (const uint8_t *)"AT+QICLOSE=0\r", 13);
    app->outreach_state = OUTREACH_MODEM_CLEANUP_QICLOSE_SENT;
    app->outreach_step_start_ms = now_ms;
}

static void begin_lan(app_context_t *app, uint32_t now_ms)
{
    char s[20];
    uint32_t ip_addr;

    /* Was: sprintf(s, "%d.%d.%d.%d", Settings.GPRSServerIP1[...]); --
     * same IP field as the modem path uses; the original has no
     * separate LAN server address despite this being a distinct
     * RemoteType branch. */
    snprintf(s, sizeof(s), "%d.%d.%d.%d",
             app->settings.gprs_server_ip1[0], app->settings.gprs_server_ip1[1],
             app->settings.gprs_server_ip1[2], app->settings.gprs_server_ip1[3]);

    /* Was resolve(s) -- scope-limited to dotted-quad parsing, see
     * ip_addr_parse.h's header note on why a full DNS resolver isn't
     * needed for this call site. Pure/instant, safe to do synchronously
     * here rather than needing its own state. */
    if (!ip_addr_parse_dotted_quad(s, &ip_addr)) {
        /* Was: printf("Cannot resolve..."); genieWriteObject(GENIE_OBJ_4DBUTTON,
         * GENIE_REMOTE, 0); Settings.RemoteType = 0; */
        app->settings.remote_type = 0;
#if APP_ENABLE_DISPLAY
        display_set_4dbutton(GENIE_REMOTE, 0);
#endif
        app->outreach_state = OUTREACH_IDLE;
        return;
    }

    if (tcp_lwip_client_connect_start(&app->lan_client_conn, ip_addr, app->settings.gprs_server_port) != 0) {
        /* Was tcp_open() failing immediately ("TCP Open failure") --
         * the original just printed and fell through with no explicit
         * RemoteType fallback here (unlike the sock_established()-failed
         * path below) -- matched as-is. */
        app->outreach_state = OUTREACH_IDLE;
        return;
    }

    app->outreach_state = OUTREACH_LAN_CONNECTING;
    app->outreach_step_start_ms = now_ms;
}

void outreach_begin(app_context_t *app, uint32_t now_ms)
{
    gprs_modem_t *m = &app->modem;

    if (app->settings.remote_type == 1) {
        if (m->gprs_state == GPRS_STATE_NOGPRS) {
            at_send(m, "AT+CSQ\r");
            app->outreach_state = OUTREACH_MODEM_CSQ_SENT;
            app->outreach_step_start_ms = now_ms;
        } else {
            /* Was the redundant-but-harmless fall-through: if
             * GPRS_STATE isn't NOGPRS when this runs (shouldn't happen
             * given the caller's own gating -- see outreach.h),
             * still_valid_connection stays 0 and the original falls
             * straight to cleanup. */
            begin_cleanup(app, now_ms);
        }
    } else if (app->settings.remote_type == 2) {
        begin_lan(app, now_ms);
    } else {
        app->outreach_state = OUTREACH_IDLE;
    }
}

int outreach_step(app_context_t *app, uint32_t now_ms)
{
    gprs_modem_t *m = &app->modem;
    int result;

    switch (app->outreach_state) {

    case OUTREACH_IDLE:
        return 0;

    case OUTREACH_MODEM_CSQ_SENT:
        result = check_step(m, "CSQ:", 1, app->outreach_step_start_ms, now_ms, 3000);
        if (result == 1) {
            char buf[100];
            snprintf(buf, sizeof(buf), "AT+QICSGP=1,1,\"%s\",\"\",\"\",3\r", app->settings.apn_name);
            at_send(m, buf);
            app->outreach_state = OUTREACH_MODEM_QICSGP_SENT;
            app->outreach_step_start_ms = now_ms;
        } else if (result == -1) {
            begin_cleanup(app, now_ms); /* valid_signal never became true */
        }
        return 0;

    case OUTREACH_MODEM_QICSGP_SENT:
        /* Was `while(...<1000){ if(GPRS_ReadSer("OK")!=0) break; yield; }`
         * -- ALWAYS proceeds to QIACT regardless of match/timeout, no
         * distinct failure path for this step specifically. */
        result = check_step(m, "OK", 0, app->outreach_step_start_ms, now_ms, 1000);
        if (result != 0) { /* matched(1) or timed out(-1) -- both proceed */
            at_send(m, "AT+QIACT=1\r");
            app->outreach_state = OUTREACH_MODEM_QIACT_SENT;
            app->outreach_step_start_ms = now_ms;
        }
        return 0;

    case OUTREACH_MODEM_QIACT_SENT:
        result = check_step(m, "OK", 1, app->outreach_step_start_ms, now_ms, 7000);
        if (result == 1) {
            char buf[100], s[20];
            snprintf(s, sizeof(s), "%d.%d.%d.%d",
                     app->settings.gprs_server_ip1[0], app->settings.gprs_server_ip1[1],
                     app->settings.gprs_server_ip1[2], app->settings.gprs_server_ip1[3]);
            snprintf(buf, sizeof(buf), "AT+QIOPEN=1,0,\"TCP\",\"%s\",%u,0,2\r",
                     s, (unsigned)app->settings.gprs_server_port);
            at_send(m, buf);
            app->outreach_state = OUTREACH_MODEM_QIOPEN_SENT;
            app->outreach_step_start_ms = now_ms;
        } else if (result == -1) {
            begin_cleanup(app, now_ms);
        }
        return 0;

    case OUTREACH_MODEM_QIOPEN_SENT:
        result = check_step(m, "CONNECT", 1, app->outreach_step_start_ms, now_ms, 5000);
        if (result == 1) {
            m->gprs_state = GPRS_STATE_CONNECTED;
            app->gprs_last_pulse_time_ms = now_ms;
            app->gprs_time_of_last_response_ms = now_ms;
            m->gprs_status = GPRS_STATUS_SOC;
            app->outreach_state = OUTREACH_IDLE;
            return 1;
        } else if (result == -1) {
            begin_cleanup(app, now_ms);
        }
        return 0;

    case OUTREACH_MODEM_CLEANUP_QICLOSE_SENT:
        result = check_step(m, "OK", 0, app->outreach_step_start_ms, now_ms, 5000);
        if (result != 0) {
            m->transport->flush_rx(m->transport->ctx);
            m->transport->write(m->transport->ctx, (const uint8_t *)"AT+QIDEACT=1\r", 13);
            app->outreach_state = OUTREACH_MODEM_CLEANUP_QIDEACT_SENT;
            app->outreach_step_start_ms = now_ms;
        }
        return 0;

    case OUTREACH_MODEM_CLEANUP_QIDEACT_SENT:
        result = check_step(m, "OK", 0, app->outreach_step_start_ms, now_ms, 5000);
        if (result != 0) {
            app->outreach_state = OUTREACH_IDLE;
        }
        return 0;

    case OUTREACH_LAN_CONNECTING: {
        int poll_result = tcp_lwip_client_connect_poll(&app->lan_client_conn, &app->lan_client_transport,
                                                         app->outreach_step_start_ms, now_ms,
                                                         LANSERVER_CONN_TIMEOUT_MS);
        if (poll_result == 1) {
            m->gprs_state = GPRS_STATE_CONNECTED;
            app->gprs_last_pulse_time_ms = now_ms;
            app->gprs_time_of_last_response_ms = now_ms;
            m->gprs_status = GPRS_STATUS_SOC;
            app->outreach_state = OUTREACH_IDLE;
            return 1;
        } else if (poll_result == -1) {
            /* Was: Settings.RemoteType = 1; Toggle_Modem(); -- fall
             * back to the modem path and wake it up. */
            app->settings.remote_type = 1;
            gprs_modem_toggle(m, 1);
            app->outreach_state = OUTREACH_IDLE;
        }
        return 0;
    }

    default:
        app->outreach_state = OUTREACH_IDLE;
        return 0;
    }
}

int outreach_in_progress(const app_context_t *app)
{
    return app->outreach_state != OUTREACH_IDLE;
}
