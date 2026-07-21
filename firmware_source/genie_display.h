/*
 * genie_display.h
 *
 * Orchestration layer for GENIE.LIB -- was the collection of globals
 * (EventQueue, pendingACK, displayDetected, etc.) plus genieBegin(),
 * genieDoEvents(), genieWriteObject(), and friends. Built on
 * genie_protocol.h's pure frame/checksum/event-queue logic.
 *
 * All the original's file-scope globals are now fields of
 * genie_display_t, so multiple instances don't collide (the original
 * assumed exactly one display). Time source is systick_ms_now() (was
 * MS_TIMER) and ms_time.h's wraparound-safe helpers (was raw
 * MS_TIMER - x comparisons).
 *
 * Serial I/O goes through genie_transport_t, matching this port's
 * established pattern (uhf_transport_t, gprs_transport_t) -- but
 * shaped differently: peek/getc/read_available mirror the original's
 * serCpeek()/serCgetc()/serCrdUsed() byte-level access exactly, since
 * genieDoEvents()'s state machine genuinely needs to peek before
 * deciding how many bytes to consume. This also makes the whole state
 * machine testable against a mock in-memory serial buffer without any
 * real hardware -- see test_genie_display.c.
 */

#ifndef GENIE_DISPLAY_H
#define GENIE_DISPLAY_H

#include "genie_protocol.h"
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GENIE_DISPLAY_TIMEOUT   2000u
#define GENIE_AUTO_PING_CYCLE   1250u

typedef struct {
    void *ctx;
    void (*write)(void *ctx, const uint8_t *buf, size_t len);
    int (*read_available)(void *ctx);
    int (*peek)(void *ctx);
    int (*getc)(void *ctx);
    void (*flush_rx)(void *ctx);
    void (*flush_tx)(void *ctx);
} genie_transport_t;

typedef void (*genie_user_handler_t)(void *user_ctx);
typedef void (*genie_magic_byte_handler_t)(void *user_ctx, uint8_t index, uint8_t len);
typedef void (*genie_magic_dbyte_handler_t)(void *user_ctx, uint8_t index, uint8_t len);
typedef void (*genie_debugger_handler_t)(void *user_ctx, const char *message);

typedef struct {
    const genie_transport_t *transport;

    genie_event_queue_t event_queue;

    int pending_ack;
    int ping_request;
    int recover_pulse;
    int auto_ping;
    uint32_t genie_cmd_timeout;
    uint32_t auto_ping_timer;
    int display_detected;
    uint32_t display_detect_timer;
    int current_form;
    int nak_inj;
    int bad_byte_counter;
    uint32_t delayed_cycles;
    uint32_t display_uptime;
    uint32_t ping_spacer;
    int genie_start;

    void *user_ctx;
    genie_user_handler_t user_handler;
    genie_magic_byte_handler_t user_magic_byte_handler;
    genie_magic_dbyte_handler_t user_magic_dbyte_handler;
    genie_debugger_handler_t user_debugger_handler;
} genie_display_t;

int genie_begin(genie_display_t *g, const genie_transport_t *transport);

int genie_read_object(genie_display_t *g, uint8_t object, uint8_t index);
int genie_write_object(genie_display_t *g, uint8_t object, uint8_t index, uint16_t data);

unsigned int genie_write_short_to_int_led_digits(genie_display_t *g, uint8_t index, int16_t data);
unsigned int genie_write_float_to_int_led_digits(genie_display_t *g, uint8_t index, float data);
unsigned int genie_write_long_to_int_led_digits(genie_display_t *g, uint8_t index, int32_t data);

int genie_write_contrast(genie_display_t *g, uint8_t value);
int genie_write_str(genie_display_t *g, uint8_t index, const char *string);
int genie_write_inh_label_default(genie_display_t *g, uint8_t index);
int genie_write_inh_label(genie_display_t *g, uint8_t index, const char *string);

int genie_do_events(genie_display_t *g);

void genie_attach_event_handler(genie_display_t *g, void *user_ctx, genie_user_handler_t handler);
void genie_attach_magic_byte_reader(genie_display_t *g, void *user_ctx, genie_magic_byte_handler_t handler);
void genie_attach_magic_double_byte_reader(genie_display_t *g, void *user_ctx, genie_magic_dbyte_handler_t handler);
void genie_attach_debugger(genie_display_t *g, void *user_ctx, genie_debugger_handler_t handler);

int genie_online(const genie_display_t *g);
uint32_t genie_uptime(const genie_display_t *g);
int genie_current_form(const genie_display_t *g);
/* Returns 1=ACK received (command confirmed applied), 0=explicit NAK,
 * -1=no reply within genie_cmd_timeout OR display not detected yet
 * (genie_write_object()'s own !display_detected guard) -- changed from
 * void 2026-07-16 so callers that need to know whether a form change
 * actually landed (not just that it was attempted) can check, instead
 * of assuming success. See app_loop.c's boot-time MAIN-activation
 * retry for the motivating case: silently discarding this return value
 * meant a single missed ACK (display detected, but this specific write
 * timed out) got marked "done" and never retried, leaving the screen
 * stuck. All other call sites are unaffected -- discarding the return
 * value is still valid C. */
int genie_activate_form(genie_display_t *g, uint8_t form);
void genie_recover(genie_display_t *g, uint8_t pulses);
int genie_timeout(genie_display_t *g, uint32_t value);
int genie_ping(genie_display_t *g);
unsigned int genie_enable_auto_ping(genie_display_t *g, uint32_t interval);

int genie_write_magic_bytes(genie_display_t *g, uint8_t index, const uint8_t *bytes, uint8_t len, int report);
int genie_write_magic_dbytes(genie_display_t *g, uint8_t index, const uint16_t *shorts, uint8_t len, int report);

int genie_get_next_byte(genie_display_t *g);
int genie_get_next_double_byte(genie_display_t *g);

int genie_dequeue_event(genie_display_t *g, genie_frame_t *out);

#ifdef __cplusplus
}
#endif

#endif /* GENIE_DISPLAY_H */
