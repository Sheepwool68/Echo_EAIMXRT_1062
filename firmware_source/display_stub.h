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

/* Was genieActivateForm(GENIE_FORM_MAIN) / GENIE_FORM_SLEEP etc */
void display_activate_form(int form_id);

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

/* Was genieWriteObject(GENIE_OBJ_4DBUTTON, ...) -- a distinct Genie
 * widget type from LED (a touchscreen button's own visual state, not
 * a status indicator light). Takes the real genie_4dbutton_t index
 * directly. Confirmed real call site: outreach.c's LAN-resolve
 * failure path (was `genieWriteObject(GENIE_OBJ_4DBUTTON, GENIE_REMOTE,
 * 0)`). */
void display_set_4dbutton(genie_4dbutton_t button, int state);

/* Was genieWriteObject(GENIE_OBJ_LED_DIGITS, ...) for the clock digits
 * and other numeric readouts. Maps 1:1 onto genie_led_digits_t. */
typedef enum {
    DISPLAY_DIGITS_MIN,
    DISPLAY_DIGITS_HOUR,
    DISPLAY_DIGITS_SEC,
    DISPLAY_DIGITS_READS,
    DISPLAY_DIGITS_BATTERY,
} display_digits_t;
void display_set_digits(display_digits_t which, int value);

/* Was genieWriteStr(GENIE_DATE_STR, ...) / GENIE_TXPDR_STR etc -- now
 * takes the real genie_string_t index directly (no real call sites
 * existed yet under the old string-name-based signature, so this
 * changed safely). */
void display_set_string(genie_string_t field, const char *value);

/* Was genieWriteObject(GENIE_OBJ_GAUGE, GENIE_GAUGE_FILE, ...) etc --
 * now takes the real genie_gauge_t index directly, same reasoning. */
void display_set_gauge(genie_gauge_t gauge, int percent);

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
 * app_loop.c's process_beeper_and_dim()). Returns 1 if touched, 0
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
 * form (see app_loop.c's process_beeper_and_dim()).
 */
int display_get_current_form(void);

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_STUB_H */
