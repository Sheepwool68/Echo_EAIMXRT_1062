/*
 * app_context.h
 *
 * Bundles every ported module's runtime state into one struct, and
 * declares the top-level init/loop entry points that replace main().
 *
 * NOT everything from the original's globals is here -- deliberately
 * excluded:
 *   - Touchscreen UI state (admin, enter_pin, keyboard_string, etc.)
 *     that's entirely driven by GENIE2.LIB's event handler, which
 *     isn't ported yet. Add these back alongside that port.
 *   - MP2731 battery-charger polling/control -- a distinct library
 *     never provided, referenced only incidentally in main()'s loop.
 *     batt_percent is kept as a plain field you can set from wherever
 *     your charger driver lives.
 */

#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <stdint.h>
#include "nrf_record.h"
#include "nrf_spi_protocol.h"
#include "tcp_session.h"
#include "tcp_transport_lwip.h"
#include "udp_discovery.h"
#include "pc_protocol.h"
#include "rtc_time.h"
#include "time_sync.h"
#include "ds3231_rt1062.h"
#include "uhf_reader.h"
#include "uhf_chip_array.h"
#include "gprs_modem.h"
#include "nand_log_littlefs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_STATE_IDLE = 0,
    APP_STATE_READING = 2,
} app_program_state_t;

typedef struct {
    /* ---- Persisted settings ---- */
    device_settings_t settings;
    uint8_t mac_address[6];

    /* ---- nRF52833 SPI link (Active/LF mode) ---- */
    nrf_spi_transport_t nrf_transport;
    uint8_t chip_reads;
    uint32_t last_log_id;
    char last_lf_code[7];
    int spi_record_state;
    uint8_t nrf_pending_record_count; /* set by a successful poll, consumed by the next retrieve -- was the original's ret_records global */
    uint8_t nrf_pending_record_type;  /* was record_type global (1 = live, else playback) */
    int encode_tag;
    int transmitter_off_pending;
    nrf_settings_cmd_t rfid_cmd;

    /* ---- TCP/UDP (server side) ---- */
    tcp_lwip_listener_t tcp_listener;
    tcp_socket_transport_t reset_transport;
    udp_transport_t discovery_transport;
    uint32_t last_status_broadcast_ms;

    /* FinishLynx integration (was FinishLynxSocket + FinishLynx_Last_Time_Sync).
     * Separate listener on FINISH_LYNX_PORT, distinct from tcp_listener
     * above -- FinishLynx speaks its own simple SOH-prefixed line
     * protocol, not the PC command protocol. The original modeled
     * exactly one client (a single socket struct); reusing this port's
     * multi-client listener infrastructure generalizes that to up to
     * TCP_LWIP_MAX_CLIENTS FinishLynx clients -- a deliberate, flagged
     * generalization, not a fidelity gap: nothing breaks if more than
     * one connects, and the original's single-client design wasn't a
     * protocol requirement, just what one raw socket naturally gives you. */
    tcp_lwip_listener_t finish_lynx_listener;
    int finish_lynx_client_was_connected[TCP_LWIP_MAX_CLIENTS];
    uint32_t finish_lynx_last_time_sync_ms;

    /* Outbound LAN client connection (Settings.RemoteType==2) -- was
     * LANClientSocket. Separate from tcp_listener above, which is only
     * ever the server side (listening for PC clients). */
    tcp_lwip_conn_t lan_client_conn;
    tcp_socket_transport_t lan_client_transport;

    /* ---- Time sync ---- */
    rtc_datetime_t current_time;
    int pps, old_pps;
    int gps_time_set;
    int set_time_nrf_pending;
    int trigger_time_flag;
    uint32_t ds_last_edge_ms;

    /* ---- UHF reader ---- */
    uhf_reader_t uhf;
    uhf_transport_t uhf_transport;
    uhf_chip_entry_t uhf_chips[UHF_CHIP_ARRAY_SIZE];
    uint32_t uhf_chip_count, uhf_unique_chips;
    int uhf_reading;
    int uhf_mode;
    app_program_state_t program_state;

    /* ---- GPRS / remote ---- */
    gprs_modem_t modem;
    gprs_transport_t gprs_transport;
    uint32_t gprs_last_process_time_ms;
    uint32_t gprs_wait_time_ms;
    uint32_t gprs_base_record;
    uint32_t gprs_last_pulse_time_ms;      /* was iGPRSLastPulseTime */
    uint32_t gprs_time_of_last_response_ms; /* was iGPRSTimeofLastResponse */
    int outreach_state;    /* outreach_state_t -- see outreach.h */
    uint32_t outreach_step_start_ms;

    /* ---- Storage ---- */
    nand_log_t log;

    /* ---- Rewind-while-reading ---- */
    int rwr_state;
    rewind_type_t rewind_type;
    uint32_t rewind_from_time, rewind_to_time;
    int rewind_socket_index;
    int only_rewind_unsent;
    int send_data_to_port;

    /* ---- Misc timers ---- */
    uint32_t last_beep_time_ms;
    int beeps_pending; /* additional beep pulses queued after the current one finishes */
    int buzzer_phase;  /* 0=idle, 1=on, 2=gap-before-next-pulse -- see app_loop.c's BUZZER_PHASE_* constants */
    uint32_t last_touch_time_s;
    uint32_t check_interval_ms;
    uint32_t check_interval2_ms;

    /* ---- Battery (charger library not provided) ---- */
    int batt_percent;
    int board_version; /* was the global board_vers -- see bms_init.h */
    uint32_t last_touch_time_ms; /* was the original's implicit touch tracking, see process_beeper_and_dim() */
    int screen_dimmed;
    int form_before_dim; /* captured via display_get_current_form() right
        before dimming, restored on wake instead of a fixed form */

} app_context_t;

/*
 * Was the setup portion of main() before the for(;;) loop (LoadSettings +
 * program_init + the various IRQ/socket/GPS/modem bring-up calls).
 * Returns 0 on success.
 */
int app_init(app_context_t *app);

/*
 * Was Beep(). Unconditionally turns the buzzer on and stamps
 * last_beep_time_ms -- matches the original exactly: Beep() itself has
 * no Settings.Beeper check, so gating on that setting is the CALLER's
 * job at whatever call site actually invokes this (not yet identified
 * from source -- see buzzer_rt1062.h). now_ms should be the same clock
 * reading app_run_one_iteration() already has for this iteration.
 */
void app_beep(app_context_t *app, uint32_t now_ms);

/*
 * Queues `count` beep pulses (each BEEP_DELAY_MS long, BEEP_GAP_MS
 * apart -- see app_loop.c), gated on Settings.Beeper. This is where
 * the setting-check the original's individual call sites presumably
 * had around their own Beep() calls now lives -- not inside app_beep()
 * itself, matching Beep()'s own unconditional behavior. Confirmed call
 * sites: a tag read (1), starting reading (2), stopping reading (1).
 */
void app_beep_n(app_context_t *app, uint32_t now_ms, int count);

/*
 * Was one pass through main()'s for(;;) body. Call repeatedly -- from
 * a bare-metal loop, or from a single FreeRTOS task with a small
 * vTaskDelay between calls (see the architecture note in the
 * conversation this was built in).
 */
void app_run_one_iteration(app_context_t *app);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONTEXT_H */
