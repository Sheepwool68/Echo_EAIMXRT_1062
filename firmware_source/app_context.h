/*
 * app_context.h
 *
 * Bundles every ported module's runtime state into one struct, and
 * declares the top-level init/loop entry points that replace main().
 *
 * UPDATE 2026-07-22: the MP2731 charge-status poll (watchdog kick +
 * charge-logo update, see process_mp2731_status() in app_loop.c) is now
 * ported -- mp2731_prev_status/mp2731_check_ms below back it. Previously
 * this file's own note said the MP2731 side was "a distinct library
 * never provided, referenced only incidentally" -- that was stale even
 * before this fix (mp2731_charger_rt1062.c/h was added for bms_init.c's
 * setpoint writes), just nothing polled REG_STATUS periodically yet.
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

    /* 1 once enet_lwip_rt1062_init() succeeds, 0 otherwise (including
     * whenever APP_ENABLE_TCP is off -- memset's 0 default is correct
     * there too). Added 2026-07-16 alongside that function's new
     * bounded-retry/non-fatal-failure behavior (see its own header
     * comment) -- app_loop.c's per-iteration TCP/ENET poll block checks
     * this before touching the netif/listeners, since none of them were
     * ever set up if ENET init failed. */
    int enet_available;

    /* One-shot guard for trace_dhcp_lease_once() (app_loop.c) -- MOVED
     * here from a `static` local, 2026-07-21, per explicit report ("not
     * reinitialising the ethernet for DHCP when I turn DHCP on... it
     * needs to show up"). A `static` local is one-shot for the entire
     * program lifetime: it correctly fires once on the FIRST lease ever
     * obtained after boot, but a `static` never re-arms, so toggling
     * DHCP off then back on later in the same session (a fresh
     * dhcp_start(), see enet_lwip_rt1062_apply_network_settings())
     * would negotiate a real new lease that the Networking form's
     * display would never learn about -- the one-shot had already
     * fired once, permanently, for this boot. The original's own
     * updateDIPA() (ACTIVERFID_V1.02_UHF.c) has no such limitation --
     * it's a real callback that fires on every DHCP success, not just
     * the first. Reset to 0 in app_genie_apply_network_settings()
     * (app_genie_dispatch.c) every time network settings are (re)applied,
     * so each new DHCP negotiation gets its own fresh "tell me when
     * this lands" arm instead of firing at most once ever. */
    int dhcp_lease_printed;

    /* 1 once display_activate_form(GENIE_FORM_MAIN) has actually been
     * sent while the display was confirmed online -- see app_loop.c's
     * per-iteration retry logic and display_stub.h's display_is_online()
     * doc comment. Added 2026-07-16 to close a real gap: app_init()'s
     * own boot-time activate-MAIN call is one-shot and silently no-ops
     * if the display wasn't detected yet at that exact moment (a real
     * risk on a genuine cold power-up without a debug probe attached,
     * where the display starts booting from scratch alongside the
     * RT1062 rather than possibly already being warm from a prior
     * probe-attached test) -- this flag lets the main loop retry once
     * the display link actually comes up, instead of leaving the
     * screen stuck on splash forever. */
    int display_main_shown;

    /* Last systick_ms_now() at which the app_loop.c retry above
     * attempted display_activate_form(GENIE_FORM_MAIN), added
     * 2026-07-16 alongside display_main_shown's success-only fix
     * (display_activate_form() can time out -- genie_write_object()'s
     * own up-to-1250ms ACK wait -- without display_detected ever
     * becoming false again, so a failed attempt must not be retried on
     * literally every single main-loop iteration; that would repeat
     * the same "retry storm blocking the loop" class of bug already
     * found and fixed once this session for GPS's PPS-triggered
     * time-sync retries). Gates retries to a fixed cooldown instead. */
    uint32_t display_main_retry_ms;

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
    int ds_rollover_seen; /* was `ds_rollover` (0 until the DS3231's first
        1Hz tick since boot, nonzero forever after -- the original never
        resets it back to 0). Sticky "time has been set at least once"
        latch; gates whether UHF tag reads get processed (see
        process_uhf_reading()'s use, matching TM_ProcessString's
        `if(ds_rollover) TM_ProcessChip(...)`) so a read at boot -- before
        the RTC has ticked, meaning the timestamp on the record would be
        garbage -- is dropped instead of logged. */

    /* ---- UHF reader ---- */
    uhf_reader_t uhf;
    uhf_transport_t uhf_transport;
    uhf_chip_entry_t uhf_chips[UHF_CHIP_ARRAY_SIZE];
    uint32_t uhf_chip_count, uhf_unique_chips;
    int uhf_reading;
    int uhf_mode;
    /* Was global `chipcode_display` (UHF_READER.LIB's TM_ProcessChip) --
     * the chip code last WRITTEN to GENIE_TXPDR_STR, so the screen only
     * gets a fresh genieWriteStr() when the read code actually changes,
     * not on every single read of the same tag. Zero-initialized by
     * app_init()'s memset(), matching the original's zero-initialized
     * global -- a real first chip code of exactly 0 can't occur (see
     * uhf_chip_array_add()'s own UHF_CHIP_IGNORED_ZERO_CODE handling). */
    uint32_t chipcode_display;
    /* Reassembly buffer for a UHF frame torn across two transport
     * reads. See process_uhf_reading()'s own doc comment (app_loop.c)
     * for the full story -- added 2026-07-17, per explicit report that
     * a part message straddling two reads was silently lost before.
     * Sized comfortably over the largest possible single frame
     * (0xFF + a 1-byte length field + 7 bytes overhead = 262 bytes max
     * for any AA-family/tag-read frame) -- uhf_process_buffer() never
     * leaves more than one incomplete frame's worth of trailing bytes
     * unconsumed, so this can never need to hold more than that. */
    uint8_t uhf_carry_buf[512];
    size_t uhf_carry_len;
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
    /* Continuous chip-read activity buzzer, added 2026-07-17 -- REPLACES
     * the old app_beep()/app_beep_n()/buzzer_phase pulse-train machinery
     * (removed the same day, having become fully dead code once this
     * took over its only two live call sites -- the start/stop-reading
     * beeps had already moved to buzzer_beep_n_blocking() earlier the
     * same session, and this is the tag-read case, the last remaining
     * caller). Per explicit report ("I want a fairly solid constant
     * buzzer when there are this many chips being continuously read...
     * timeout where the buzzer stops if not one chip is read after say
     * 200ms") -- see uhf_event_cb()/process_uhf_read_buzzer() in
     * app_loop.c for the actual state machine (turn on on a read if not
     * already on; turn off once UHF_READ_BUZZER_TIMEOUT_MS has elapsed
     * since the last one). uhf_last_read_ms is only meaningful while
     * uhf_read_buzzer_on is set. */
    uint32_t uhf_last_read_ms;
    int uhf_read_buzzer_on;
    /* Same mechanism, separate state, for Active/LF (nRF) chip reads --
     * was `Beep()`/`LastBeepTime`/`BEEPDELAY` in comms_NRF()'s case 0x02
     * (record_type==1 branch, `ACTIVERFID_V1.02_UHF.c` line 1113:
     * `if(Settings.Beeper) Beep();`, one call per retrieved record) --
     * a GENERIC mechanism in the original (one shared timer for every
     * Beep() caller, UHF and Active/LF alike), unlike this port's
     * UHF-specific process_uhf_read_buzzer(), which was purpose-built
     * for a UHF-specific tuning request and never intended as a stand-in
     * for the original's separate Active/LF beep call site -- that call
     * site was never ported by ANY version of this port's buzzer code,
     * old or new (see process_nrf_spi()'s retrieve branch, app_loop.c).
     * Kept as separate fields/state (not merged into the uhf_* pair)
     * so this fix can't regress the already hardware-confirmed UHF
     * buzzer behavior. Reuses UHF_READ_BUZZER_TIMEOUT_MS (300ms) since
     * that constant IS BEEPDELAY, the same original value, not a new
     * UHF-specific one. */
    uint32_t nrf_last_read_ms;
    int nrf_read_buzzer_on;
    /* Was global `toggle` (zero-initialized in program_init(), matches
     * this field's memset() default) -- the button LED's blink phase
     * while ProgramState==READING. See app_loop.c's DS3231 rollover
     * block (was `if(ProgramState==READING){ BitWrPortI(PBDR,
     * &PBDRShadow,toggle,6); toggle^=1; }`, ACTIVERFID_V1.02_UHF.c
     * lines 3605-3607) -- added 2026-07-17 per explicit request. */
    int button_led_blink_state;
    uint32_t last_touch_time_s;
    uint32_t check_interval_ms;
    uint32_t check_interval2_ms;

    /* ---- Battery ---- */
    int batt_percent;
    int board_version; /* was the global board_vers -- see bms_init.h */
    uint32_t mp2731_check_ms; /* was the same checkInterval-style 1Hz
        cadence as the DS3231 rollover block in the original, decoupled
        into its own periodic check here per app_loop.c's own note --
        see process_mp2731_status(). */
    int mp2731_prev_status; /* was the global prev_char -- -1 sentinel
        (not a valid 8-bit register value) so the very first read always
        counts as "changed" and updates the charge logo immediately,
        instead of possibly matching a stale zero-initialized value. */
    uint32_t last_touch_time_ms; /* was the original's implicit touch tracking, see process_display_events() */
    int screen_dimmed;
    int form_before_dim; /* captured via display_get_current_form() right
        before dimming, restored on wake instead of a fixed form */

    /* ---- Touchscreen UI state (was myGenieEventHandler()'s globals) ---- */
    int admin;          /* factory/PIN-gated features unlock */
    int enter_pin;       /* mid-PIN-entry flag -- next KEYPAD Enter is
        checked against the admin PIN rather than acted on directly */
    char keyboard_string[32]; /* shared KEYBOARD+KEYPAD text-entry buffer.
        Original's own cap checks (<29 for KEYBOARD, <21 for KEYPAD) bound
        it but no explicit `char keyboard_string[N]` declaration was in
        the pasted source -- 32 is an inferred safe size, not a confirmed
        original constant. */
    int last_winbutton;  /* which WinButton opened the current KEYBOARD/
        KEYPAD form, so its Enter-key handler knows what the finished
        string means. -1 = none (was implicitly 0 in the original, but
        0 collides with the real GENIE_BUTTON_SETTINGS enum value here;
        moot in practice since nothing reads this before a WinButton
        press sets it first). */
    int time_offset;     /* pending timezone delta (hours), set by the
        TIMEZ trackbar, applied and reset to 0 by the FORM_MAIN refresh
        (was updateGenie_Main()'s `if(time_offset<0 || time_offset>0)`
        block) */
    int diag_visible;    /* gates antenna/RSSI gauge display updates in
        the original (UHF_READER.LIB: `if(diag_visible)
        genieWriteObject(GENIE_OBJ_GAUGE, ...)`- this dispatcher only SETS
        this field; wiring uhf_reader.c/app_loop.c's own gauge writes to
        consult it is a separate, not-yet-done follow-up. */
    int lo_backlight;     /* write-only from this dispatcher (Sleep-form
        wake resets it to 0); its consumer -- a battery-percent-driven
        backlight-reduction check in the original's separate periodic
        battery-check loop -- isn't ported here either, same follow-up
        note as diag_visible above. */
    int new_firmware_avail; /* -1=error, 0=up to date, 1=update available;
        drives GENIE_FW_PROGRESS_STR. Only ever populated by the stubbed
        check-firmware path (see app_genie_dispatch.h) until that gets a
        real HTTP-backed implementation. */
    uint16_t new_fw_vers;   /* paired with the above */

} app_context_t;

/*
 * Was the setup portion of main() before the for(;;) loop (LoadSettings +
 * program_init + the various IRQ/socket/GPS/modem bring-up calls).
 * Returns 0 on success.
 */
int app_init(app_context_t *app);

/*
 * Was ACTIVERFID_V1.02_UHF.c lines 3732-3742 (the checkInterval battery
 * block) -- reads MAX17303_REG_REPSOC and scales it into app->batt_percent
 * (no-op unless APP_ENABLE_BMS is on -- no longer also gated on
 * app->board_version>=32, see the implementation's own 2026-07-22
 * comment in app_loop.c: older, <32 boards are out of scope for this
 * processor entirely).
 * Exposed here (rather than kept static in app_loop.c) so app_init()
 * can call it once immediately, before the first display paint --
 * added 2026-07-16 per explicit request that the battery bar/value
 * show a real reading immediately, not wait for process_periodic_checks()'s
 * first fire 2 seconds into the main loop. process_periodic_checks()
 * itself now just calls this on its own repeat timer.
 *
 * Returns 1 if it actually ran (nRF SPI ready line not asserted -- see
 * app_loop.c's definition for why this gate was restored 2026-07-20),
 * 0 if skipped/deferred. Boot-time callers can ignore the return value.
 */
int app_update_battery_percent(app_context_t *app);

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
