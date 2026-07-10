/*
 * display_stub.c
 *
 * Real implementation of display_stub.h, backed by genie_display.h +
 * genie_transport_rt1062.c. Uses a single module-static genie_display_t
 * instance, matching the original GENIE.LIB's own single-display,
 * global-state design (this app only ever has one touchscreen).
 */

#include "display_stub.h"
#include "genie_display.h"
#include "genie_transport_rt1062.h"

static genie_display_t s_genie;
static genie_transport_t s_genie_transport;

int display_init(void)
{
    s_genie_transport = genie_transport_rt1062_init();
    return genie_begin(&s_genie, &s_genie_transport);
}

void display_do_events(void)
{
    genie_do_events(&s_genie);
}

void display_show_splash(const char *msg)
{
    genie_write_str(&s_genie, GENIE_SPLASH_STR, msg);
}

void display_activate_form(int form_id)
{
    genie_activate_form(&s_genie, (uint8_t)form_id);
}

void display_set_contrast(uint8_t level)
{
    genie_write_contrast(&s_genie, level);
}

void display_set_led(display_led_t led, int on)
{
    uint8_t genie_index;

    switch (led) {
    case DISPLAY_LED_PPS:            genie_index = GENIE_LED_PPS;  break;
    case DISPLAY_LED_LOCAL_SOCKET:   genie_index = GENIE_LED_LSOC; break;
    case DISPLAY_LED_GPRS_SOCKET:    genie_index = GENIE_LED_GSOC; break;
    case DISPLAY_LED_LOOP:           genie_index = GENIE_LED_LOOP; break;
    case DISPLAY_LED_PLAYBACK:       genie_index = GENIE_LED_PBACK;break;
    case DISPLAY_LED_BT_ON:          genie_index = GENIE_LED_BTON; break;
    case DISPLAY_LED_LOW_POWER_MODE: genie_index = GENIE_LED_LPM;  break;
    default:
        return;
    }
    genie_write_object(&s_genie, GENIE_OBJ_LED, genie_index, (uint16_t)(on ? 1 : 0));
}

void display_set_4dbutton(genie_4dbutton_t button, int state)
{
    genie_write_object(&s_genie, GENIE_OBJ_4DBUTTON, (uint8_t)button, (uint16_t)state);
}

void display_set_digits(display_digits_t which, int value)
{
    uint8_t genie_index;

    switch (which) {
    case DISPLAY_DIGITS_MIN:     genie_index = GENIE_DLED_MIN;  break;
    case DISPLAY_DIGITS_HOUR:    genie_index = GENIE_DLED_HOUR; break;
    case DISPLAY_DIGITS_SEC:     genie_index = GENIE_DLED_SEC;  break;
    case DISPLAY_DIGITS_READS:   genie_index = GENIE_DLED_READS;break;
    case DISPLAY_DIGITS_BATTERY: genie_index = GENIE_DLED_BAT;  break;
    default:
        return;
    }
    genie_write_object(&s_genie, GENIE_OBJ_LED_DIGITS, genie_index, (uint16_t)value);
}

void display_set_string(genie_string_t field, const char *value)
{
    genie_write_str(&s_genie, (uint8_t)field, value);
}

void display_set_gauge(genie_gauge_t gauge, int percent)
{
    genie_write_object(&s_genie, GENIE_OBJ_GAUGE, (uint8_t)gauge, (uint16_t)percent);
}

void display_set_tank(genie_tank_t tank, int percent)
{
    genie_write_object(&s_genie, GENIE_OBJ_TANK, (uint8_t)tank, (uint16_t)percent);
}

int display_dequeue_event(display_event_t *out)
{
    genie_frame_t f;

    while (genie_dequeue_event(&s_genie, &f)) {
        if (f.cmd != GENIE_PING) {
            out->object = f.object;
            out->index = f.index;
            out->data = (int)genie_frame_get_data(&f);
            return 1;
        }
        /* was a ping-status event, not real user activity -- keep
         * draining until a real event or the queue is empty */
    }
    return 0;
}

int display_get_current_form(void)
{
    return genie_current_form(&s_genie);
}
