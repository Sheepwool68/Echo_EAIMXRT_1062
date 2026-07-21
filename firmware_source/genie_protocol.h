/*
 * genie_protocol.h
 *
 * Ported from GENIE.LIB (RFID Timing's Dynamic C port of 4D Systems'
 * ViSi-Genie library, based on their public Arduino library v1.4.5).
 * Pure logic layer: enums, checksum computation, frame building, and
 * the event queue -- no serial I/O here, all of that lives in
 * genie_display.h's orchestration layer. Ported as close to the
 * original as the language change allows, per explicit request --
 * names and structure preserved, only the I/O boundary moved.
 *
 * Every enum value below is copied verbatim (same names, same
 * implicit ordinal values) from the pasted source, including the
 * app-specific additions (Forms, 4D buttons, WinButtons, Strings,
 * Gauges, Keyboards, LEDs, Tanks, LED Digits, Trackbars) that aren't
 * part of stock 4D Systems ViSi-Genie.
 */

#ifndef GENIE_PROTOCOL_H
#define GENIE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GENIE_FRAME_SIZE     6
#define MAX_GENIE_EVENTS     16

#define GENIE_ACK               0x06
#define GENIE_NAK               0x15
#define GENIE_PING              0x80
#define GENIE_READY             0x81
#define GENIE_DISCONNECTED      0x82

typedef enum {
    GENIE_READ_OBJ,
    GENIE_WRITE_OBJ,
    GENIE_WRITE_STR,
    GENIE_WRITE_STRU,
    GENIE_WRITE_CONTRAST,
    GENIE_REPORT_OBJ,
    GENIE_REPORT_EVENT = 7,
    GENIEM_WRITE_BYTES,
    GENIEM_WRITE_DBYTES,
    GENIEM_REPORT_BYTES,
    GENIEM_REPORT_DBYTES,
    GENIE_WRITE_INH_LABEL
} genie_command_t;

typedef enum {
    GENIE_OBJ_DIPSW,
    GENIE_OBJ_KNOB,
    GENIE_OBJ_ROCKERSW,
    GENIE_OBJ_ROTARYSW,
    GENIE_OBJ_SLIDER,
    GENIE_OBJ_TRACKBAR,
    GENIE_OBJ_WINBUTTON,
    GENIE_OBJ_ANGULAR_METER,
    GENIE_OBJ_COOL_GAUGE,
    GENIE_OBJ_CUSTOM_DIGITS,
    GENIE_OBJ_FORM,
    GENIE_OBJ_GAUGE,
    GENIE_OBJ_IMAGE,
    GENIE_OBJ_KEYBOARD,
    GENIE_OBJ_LED,
    GENIE_OBJ_LED_DIGITS,
    GENIE_OBJ_METER,
    GENIE_OBJ_STRINGS,
    GENIE_OBJ_THERMOMETER,
    GENIE_OBJ_USER_LED,
    GENIE_OBJ_VIDEO,
    GENIE_OBJ_STATIC_TEXT,
    GENIE_OBJ_SOUND,
    GENIE_OBJ_TIMER,
    GENIE_OBJ_SPECTRUM,
    GENIE_OBJ_SCOPE,
    GENIE_OBJ_TANK,
    GENIE_OBJ_USERIMAGES,
    GENIE_OBJ_PINOUTPUT,
    GENIE_OBJ_PININPUT,
    GENIE_OBJ_4DBUTTON,
    GENIE_OBJ_ANIBUTTON,
    GENIE_OBJ_COLORPICKER,
    GENIE_OBJ_USERBUTTON,
    GENIE_OBJ_MAGIC_RESERVED,
    GENIE_OBJ_SMARTGAUGE,
    GENIE_OBJ_SMARTSLIDER,
    GENIE_OBJ_SMARTKNOB,
    GENIE_OBJ_ILED_DIGITS_H,
    GENIE_OBJ_IANGULAR_METER,
    GENIE_OBJ_IGAUGE,
    GENIE_OBJ_ILABELB,
    GENIE_OBJ_IUSER_GAUGE,
    GENIE_OBJ_IMEDIA_GAUGE,
    GENIE_OBJ_IMEDIA_THERMOMETER,
    GENIE_OBJ_ILED,
    GENIE_OBJ_IMEDIA_LED,
    GENIE_OBJ_ILED_DIGITS_L,
    GENIE_OBJ_ILED_DIGITS,
    GENIE_OBJ_INEEDLE,
    GENIE_OBJ_IRULER,
    GENIE_OBJ_ILED_DIGIT,
    GENIE_OBJ_IBUTTOND,
    GENIE_OBJ_IBUTTONE,
    GENIE_OBJ_IMEDIA_BUTTON,
    GENIE_OBJ_ITOGGLE_INPUT,
    GENIE_OBJ_IDIAL,
    GENIE_OBJ_IMEDIA_ROTARY,
    GENIE_OBJ_IROTARY_INPUT,
    GENIE_OBJ_ISWITCH,
    GENIE_OBJ_ISWITCHB,
    GENIE_OBJ_ISLIDERE,
    GENIE_OBJ_IMEDIA_SLIDER,
    GENIE_OBJ_ISLIDERH,
    GENIE_OBJ_ISLIDERG,
    GENIE_OBJ_ISLIDERF,
    GENIE_OBJ_ISLIDERD,
    GENIE_OBJ_ISLIDERC,
    GENIE_OBJ_ILINEAR_INPUT
} genie_object_t;

typedef enum {
    GENIE_FORM_SPLASHSCREEN,
    GENIE_FORM_MAIN,
    GENIE_FORM_SETTINGS,
    GENIE_FORM_NETWORKING,
    GENIE_FORM_RFID,
    GENIE_FORM_KEYBOARD,
    GENIE_FORM_KEYPAD,
    GENIE_FORM_DATA,
    GENIE_FORM_TIME,
    GENIE_FORM_SLEEP,
    GENIE_FORM_OTHER,
    GENIE_FORM_UHF,
    GENIE_FORM_ANT
} genie_form_t;

typedef enum {
    GENIE_SYSTEM,
    GENIE_RFID,
    GENIE_NET,
    GENIE_DATA,
    GENIE_TIME,
    GENIE_DHCP,
    GENIE_REMOTE,
    GENIE_GPS,
    GENIE_ADD30,
    GENIE_PLAYBACK,
    GENIE_SLEEP,
    GENIE_OTHER,
    GENIE_BUZZER,
    GENIE_DIM,
    GENIE_TRIGGER,
    GENIE_UHF_MODE,
    GENIE_UHF_READ
} genie_4dbutton_t;

typedef enum {
    GENIE_BUTTON_SETTINGS,
    GENIE_BUTTON_RESET,
    GENIE_BUTTON_MAIN,
    GENIE_BUTTON_BACK,
    GENIE_BUTTON_SETLAN,
    GENIE_BUTTON_SETAPN,
    GENIE_BUTTON_SETREMOTE_IP,
    GENIE_BUTTON_SETREMOTE_PORT,
    GENIE_BUTTON_BACK2,
    GENIE_BUTTON_BACK3,
    GENIE_BUTTON_BACK_OTHER,
    GENIE_BUTTON_BACK4,
    GENIE_BUTTON_CLEAR_LOG,
    GENIE_BUTTON_UPDATE_FIRMWARE,
    GENIE_BUTTON_CANCEL,
    GENIE_BUTTON_CODE,
    GENIE_BUTTON_CHECK,
    GENIE_BUTTON_SETGATEWAY_IP,
    GENIE_BUTTON_UHF_CHANGE,
    GENIE_BUTTON_BACK5,
    GENIE_BUTTON_BACK6,
    GENIE_BUTTON_CHECK_RL,
    GENIE_BUTTON_DIAGNOSTICS
} genie_winbutton_t;

typedef enum {
    GENIE_SPLASH_STR,
    GENIE_TXPDR_STR,
    GENIE_LAN_STR,
    GENIE_REMOTE_STR,
    GENIE_PORT_STR,
    GENIE_APN_STR,
    GENIE_KB_STR,
    GENIE_KP_STR,
    GENIE_RECORDS_STR,
    GENIE_DATE_STR,
    GENIE_CODE_STR,
    GENIE_SIGN_STR,
    GENIE_LAST_READ_STR,
    GENIE_INSTFW_STR,
    GENIE_CURRFW_STR,
    GENIE_TXPDR_BAT_STR,
    GENIE_FW_PROGRESS_STR,
    GENIE_MAC_STR,
    GENIE_GATEWAYIP_STR,
    GENIE_STR_DUNNO,
    GENIE_STR_DUNNO2,
    GENIE_SYSTEM_STR,
    GENIE_LED1_STR,
    GENIE_LED2_STR,
    GENIE_LED3_STR,
    GENIE_LED4_STR,
    GENIE_BAT_OR_STATUS_STR,
    GENIE_STR_DUNNO3,
    GENIE_COUNTRY_STR,
    GENIE_FREQ_STR
} genie_string_t;

typedef enum {
    GENIE_GAUGE_PWR,
    GENIE_GAUGE_BAT,
    GENIE_GAUGE_FILE,
    GENIE_GAUGE_ANT1,
    GENIE_GAUGE_ANT2,
    GENIE_GAUGE_ANT3,
    GENIE_GAUGE_ANT4
} genie_gauge_t;

typedef enum {
    GENIE_KEYBOARD,
    GENIE_KEYPAD
} genie_keyboard_t;

typedef enum {
    GENIE_LED_PPS,
    GENIE_LED_GSOC,
    GENIE_LED_LSOC,
    GENIE_LED_LOOP,
    GENIE_LED_PBACK,
    GENIE_LED_BTON,
    GENIE_LED_LPM
} genie_led_t;

typedef enum {
    GENIE_TANK_4G,
    GENIE_TANK_GPS
} genie_tank_t;

typedef enum {
    GENIE_DLED_READS,
    GENIE_DLED_BAT,
    GENIE_DLED_POWER,
    GENIE_DLED_HOUR,
    GENIE_DLED_MIN,
    GENIE_DLED_SEC,
    GENIE_DLED_POWER_CHANGE,
    GENIE_DLED_ID,
    GENIE_DLED_BT_TIME_ON,
    GENIE_DLED_ID1,
    GENIE_DLED_TIMEZONE,
    GENIE_DLED_ID2
} genie_led_digits_t;

typedef enum {
    GENIE_TB_ID,
    GENIE_TB_BT_ON,
    GENIE_TB_ID2,
    GENIE_TB_TIMEZ
} genie_trackbar_t;

/* Single-widget-per-form indices, confirmed from Genie2.lib
 * (`static char GENIE_KNOB_PWR = 0;` / `static char GENIE_SLIDER_DIM = 0;`)
 * -- there's only ever one Knob (reader power) and one Slider
 * (brightness/dim) on their respective forms, so unlike the multi-member
 * enums above these are single-value "enums" of one, not a guess. */
typedef enum {
    GENIE_KNOB_PWR = 0
} genie_knob_t;

typedef enum {
    GENIE_SLIDER_DIM = 0
} genie_slider_t;

typedef struct {
    uint8_t cmd;
    uint8_t object;
    uint8_t index;
    uint8_t data_msb;
    uint8_t data_lsb;
} genie_frame_t;

typedef struct {
    genie_frame_t frames[MAX_GENIE_EVENTS];
    uint8_t rd_index;
    uint8_t wr_index;
    uint8_t n_events;
} genie_event_queue_t;

uint8_t genie_checksum(const uint8_t *data, size_t len);

int genie_build_read_obj_frame(uint8_t object, uint8_t index, uint8_t *out);

int genie_build_write_obj_frame(uint8_t object, uint8_t index, uint16_t data, uint8_t *out);

int genie_build_write_contrast_frame(uint8_t value, uint8_t *out);

int genie_build_write_str_frame(uint8_t index, const char *string, uint8_t *out, size_t out_size);

int genie_build_write_inh_label_frame(uint8_t index, const char *string, uint8_t *out, size_t out_size);

int genie_build_magic_bytes_frame(uint8_t index, const uint8_t *bytes, uint8_t len, uint8_t *out);

int genie_build_magic_dbytes_frame(uint8_t index, const uint16_t *shorts, uint8_t len, uint8_t *out);

int genie_parse_report_frame(const uint8_t bytes[GENIE_FRAME_SIZE], genie_frame_t *out_frame);

void genie_event_queue_init(genie_event_queue_t *q);

int genie_event_queue_enqueue(genie_event_queue_t *q, const genie_frame_t *frame);

int genie_event_queue_dequeue(genie_event_queue_t *q, genie_frame_t *out);

int genie_frame_is(const genie_frame_t *f, uint8_t cmd, uint8_t object, uint8_t index);

uint16_t genie_frame_get_data(const genie_frame_t *f);

#ifdef __cplusplus
}
#endif

#endif /* GENIE_PROTOCOL_H */
