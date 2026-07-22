/*
 * display_stub.h
 *
 * UPDATE: GENIE.LIB has now been pasted and ported (genie_protocol.h +
 * genie_display.h) -- display_stub.c implements every function below
 * for real now, backed by that real state machine + genie_transport_rt1062.c.
 * The name "display_stub.h" and its function signatures are kept as
 * the stable interface app_init.c/app_loop.c/app_pc_dispatch.c already
 * call into -- no call-site changes needed for the 3 that existed
 * before this update (display_show_splash, display_activate_form,
 * display_do_events). display_set_string/display_set_gauge had no real
 * call sites yet, so their signatures were updated to take the actual
 * genie_string_t/genie_gauge_t enums directly instead of the
 * placeholder string-based dispatch this file used before the real
 * library was available.
 */

#ifndef DISPLAY_STUB_H
#define DISPLAY_STUB_H

#include <stdint.h>
#include "genie_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at boot, after the LPUART transport is ready -- was
 * genieBegin(). Not in the original display_stub.h interface (there
 * was nothing to initialize against); added now that there's a real
 * display to connect to. */
int display_init(void);

/* Was genieDoEvents() -- must be called every main loop iteration to
 * pump the display's own event queue (touch events, etc). */
void display_do_events(void);

/* Was genieWriteStr(GENIE_SPLASH_STR, ...) during boot */
void display_show_splash(const char *msg);

/* Was genieActivateForm(GENIE_FORM_MAIN) / GENIE_FORM_SLEEP etc.
 * Returns 1=ACK/confirmed, 0=NAK, -1=timeout or not-yet-detected --
 * see genie_activate_form()'s doc comment. Safe to discard at any
 * existing call site (all pre-2026-07-16 callers do); only added so a
 * caller that needs delivery confirmation (see app_loop.c's boot-time
 * MAIN-activation retry) can get it. */
int display_activate_form(int form_id);

/* Was genieWriteContrast(...) -- 0 = dim/off, up to Settings.Brightness */
void display_set_contrast(uint8_t level);

/* Was the various genieWriteObject(GENIE_OBJ_LED, ...) calls for
 * status LEDs (PPS, socket connected, GPRS connected, reading state).
 * Maps 1:1 onto genie_led_t (see display_stub.c) -- kept as its own
 * enum rather than genie_led_t directly since these names read more
 * clearly at the call sites that predate the real library. */
typedef enum {
    DISPLAY_LED_PPS,
    DISPLAY_LED_LOCAL_SOCKET,
    DISPLAY_LED_GPRS_SOCKET,
    DISPLAY_LED_LOOP,
    DISPLAY_LED_PLAYBACK,
    DISPLAY_LED_BT_ON,
    DISPLAY_LED_LOW_POWER_MODE,
} display_led_t;
void display_set_led(display_led_t led, int on);

/* Was `genieWriteObject(GENIE_OBJ_USERIMAGES, 0x01, reg_char ? 1 : 0)`
 * (ACTIVERFID_V1.02_UHF.c line 3679/3681) -- shows/hides the battery
 * charge-logo image based on the MP2731's charging-status bits. Added
 * 2026-07-22 per explicit report ("do not see the battery charge
 * logo") -- this whole poll was previously flagged as not-yet-ported
 * (see app_loop.c's process_mp2731_status()). The original's widget
 * index (0x01) has no named constant in genie_protocol.h -- ported as
 * a literal, matching source exactly. */
void display_set_charge_logo(int charging);

/* Was genieWriteObject(GENIE_OBJ_4DBUTTON, ...) -- a distinct Genie
 * widget type from LED (a touchscreen button's own visual state, not
 * a status indicator light). Takes the real genie_4dbutton_t index
 * directly. Confirmed real call site: outreach.c's LAN-resolve
 * failure path (was `genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_REMOTE,
 * 0)`). */
void display_set_4dbutton(genie_4dbutton_t button, int state);

/* Was genieWriteObject(GENIE_OBJ_LED_DIGITS, ...) for the clock digits
 * and other numeric readouts. CHANGED 2026-07-15 from a local 5-member
 * wrapper enum (MIN/HOUR/SEC/READS/BATTERY) to the real genie_led_digits_t
 * directly -- app_genie_dispatch.c's touchscreen UI port needs 7 more
 * LED-digit targets (POWER, ID, BT_TIME_ON, ID1, TIMEZONE, ID2,
 * POWER_CHANGE) that the old wrapper didn't cover, and since
 * genie_led_digits_t already enumerates the complete real set (unlike
 * display_led_t's 7-of-7 for LEDs, this was an incomplete subset), a
 * translated wrapper here was only ever going to keep growing --
 * matches how display_set_4dbutton/display_set_gauge/display_set_tank/
 * display_set_string already take their real genie_*_t enum directly. */
/* Returns 1=ACK, 0=NAK, -1=timeout/not-detected (same as
 * display_activate_form()) -- added 2026-07-16 per explicit report
 * that the seconds digit on MAIN wasn't ticking reliably right after
 * boot. This write uses the exact same genie_write_object() ACK-wait
 * mechanism as display_activate_form(), which is known (this session)
 * to sometimes time out -- previously that failure was completely
 * silent here, discarded by the caller, with no way to tell "digit
 * write failed this tick" from "digit write never even attempted."
 * Safe to discard at existing call sites. */
int display_set_digits(genie_led_digits_t which, int value);

/* Was genieWriteStr(GENIE_DATE_STR, ...) / GENIE_TXPDR_STR etc -- now
 * takes the real genie_string_t index directly (no real call sites
 * existed yet under the old string-name-based signature, so this
 * changed safely). */
void display_set_string(genie_string_t field, const char *value);

/* Was genieWriteObject(GENIE_OBJ_TRACKBAR, ...) -- writes a Trackbar
 * widget's slider position (distinct from GENIE_OBJ_SLIDER below, and
 * from GENIE_OBJ_LED_DIGITS -- three different Genie widget types that
 * happen to all display numbers). Added for app_genie_dispatch.c, the
 * first real call sites for this object type. */
void display_set_trackbar(genie_trackbar_t trackbar, int value);

/* Was genieWriteObject(GENIE_OBJ_SLIDER, ...) -- the brightness/dim
 * slider's own position (GENIE_SLIDER_DIM is its only index, see
 * genie_protocol.h). Same reasoning as display_set_trackbar above. */
void display_set_slider(genie_slider_t slider, int value);

/* Was genieWriteObject(GENIE_OBJ_STRINGS, ...) -- a DIFFERENT Genie
 * widget from the literal-text write above: GENIE_OBJ_STRINGS selects
 * among several pre-programmed string variants by a numeric index (e.g.
 * `genieWriteObject(GENIE_OBJ_STRINGS, GENIE_SYSTEM_STR, Settings.System)`
 * picks which canned string to show based on Settings.System's value),
 * not literal app-supplied text. Added for app_genie_dispatch.c's
 * FORM_MAIN/RFID-form refresh, the first real call sites for this
 * object type in this port. */
void display_set_string_index(genie_string_t field, int value);

/* Was genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_FILE, ...) etc --
 * now takes the real genie_gauge_t index directly, same reasoning. */
void display_set_gauge(genie_gauge_t gauge, int percent);

/* Was genieWriteObject(GENIE_OBJ_KNOB, GENIE_KNOB_PWR, Settings.ReaderPower)
 * -- the reader-power knob on the RFID (Active/LF) form. ADDED 2026-07-20:
 * this port previously substituted a GENIE_OBJ_GAUGE write here instead
 * (display_set_gauge(GENIE_GAUGE_PWR, ...)), assumed to be an intentional
 * display redesign since it was used consistently at all 3 call sites --
 * WRONG, per explicit user correction ("when I get to RFID form I do not
 * see you updating GENIE_KNOB_PWR") confirming the real widget on the
 * physical display is a Knob, not a Gauge. */
void display_set_knob(genie_knob_t knob, int percent);

/* Was genieWriteObject(GENIE_OBJ_TANK, GENIE_TANK_GPS, ...) -- a
 * distinct Genie widget type from Gauge, used by NEOM8T.LIB's
 * GetGPS_Signal() for the satellite-count tank display. */
void display_set_tank(genie_tank_t tank, int percent);

/*
 * Drains the display's event queue and reports whether any GENUINE
 * touch/interaction event was seen since the last call (WinButton,
 * 4DButton, slider, trackbar, etc) -- specifically excluding the
 * display's own connect/disconnect/ping status events (cmd==GENIE_PING),
 * which land in the same queue but aren't user activity. Call once per
 * main loop iteration; used for dim/screensaver timeout logic (see
 * app_loop.c's process_display_events()). Returns 1 if touched, 0
 * otherwise. Draining the whole queue every call also matters for its
 * own sake -- an unconsumed queue nearing MAX_GENIE_EVENTS-2 stops
 * accepting new events (see genie_protocol.h's event queue notes).
 */
/*
 * Was genie_dequeue_event() exposed one level up, with a real event
 * type instead of raw genie_frame_t (keeping genie_protocol.h's types
 * out of display_stub.h's callers). Excludes the display's own
 * connect/disconnect/ping status events (cmd==GENIE_PING), which land
 * in the same queue but aren't user activity -- same filter
 * display_had_touch_activity() used to apply. This is now the ONLY
 * function that drains the display's event queue; call it in a loop
 * each main iteration (see app_loop.c's process_display_events()) --
 * an unconsumed queue nearing MAX_GENIE_EVENTS-2 stops accepting new
 * events (see genie_protocol.h's event queue notes).
 */
typedef struct {
    int object; /* genie_object_t */
    int index;
    int data;   /* combined data_msb/data_lsb, was genieGetEventData() */
} display_event_t;

int display_dequeue_event(display_event_t *out);

/*
 * Was genieCurrentForm() -- the display's own tracked "currently
 * active form" (updated internally whenever the display reports a
 * form change). Used to capture the form active just before dimming,
 * so waking up can restore it exactly rather than jumping to a fixed
 * form (see app_loop.c's process_display_events()).
 */
int display_get_current_form(void);

/* Was genieOnline() -- true once the display has ACTUALLY replied to a
 * ping/read-form request (see genie_display.c's genie_ping()/
 * genie_do_events()), not just "we tried to talk to it." Added
 * 2026-07-16: display_init()'s own detection can miss its narrow
 * boot-time window on a genuinely cold power-up (no debug probe
 * attached -- the display starts booting at the same instant as the
 * RT1062, unlike a probe-attached reflash where the display may
 * already be warm from a prior test), but the display link recovers
 * on its own moments later via the ongoing 50ms-interval auto-ping
 * (see display_do_events()) -- this just wasn't previously visible or
 * acted on. Use this to retry a one-shot boot-time display command
 * (like activating MAIN) once the link actually comes up, instead of
 * only trying once during display_init() and giving up silently if
 * that narrow window is missed. */
int display_is_online(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_STUB_H */
