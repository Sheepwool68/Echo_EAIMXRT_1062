/*
 * genie_display.c
 *
 * See genie_display.h. This is I/O choreography, same honest scope
 * note as uhf_reader.c/gprs_modem.c -- but unlike those, the state
 * machine here is fully testable against a MOCK transport (an
 * in-memory fake serial buffer), since genie_transport_t's
 * peek/getc/read_available shape doesn't actually require real
 * hardware to exercise. See test_genie_display.c for thorough
 * coverage of every quirk traced from the original source.
 */

#include "genie_display.h"
#include "systick_ms_rt1062.h"
#include "ms_time.h"
#include <string.h>

static void enqueue_ping_event(genie_display_t *g, uint8_t status)
{
    genie_frame_t f;
    f.cmd = GENIE_PING;
    f.object = status;
    f.index = 0;
    f.data_msb = 0;
    f.data_lsb = 0;
    genie_event_queue_enqueue(&g->event_queue, &f);
}

int genie_begin(genie_display_t *g, const genie_transport_t *transport)
{
    uint32_t timeout_start;

    memset(g, 0, sizeof(*g));
    g->transport = transport;
    genie_event_queue_init(&g->event_queue);

    g->recover_pulse = 50;
    g->genie_cmd_timeout = 1250;
    g->current_form = -1;
    g->genie_start = 1;

    g->display_uptime = systick_ms_now();
    genie_read_object(g, GENIE_OBJ_FORM, 0);

    timeout_start = systick_ms_now();
    while (ms_elapsed(systick_ms_now(), timeout_start) <= 150u) {
        if (genie_do_events(g) == GENIE_REPORT_OBJ && !g->genie_start) {
            return 1;
        }
    }
    g->display_detected = 0;
    return 0;
}

int genie_read_object(genie_display_t *g, uint8_t object, uint8_t index)
{
    uint8_t frame[4];
    if (!g->display_detected) {
        return -1;
    }
    genie_build_read_obj_frame(object, index, frame);
    g->transport->write(g->transport->ctx, frame, 4);
    return 0;
}

int genie_write_object(genie_display_t *g, uint8_t object, uint8_t index, uint16_t data)
{
    uint8_t frame[6];
    uint32_t timeout_write;

    if (!g->display_detected) {
        return -1;
    }
    g->pending_ack = 1;
    genie_build_write_obj_frame(object, index, data, frame);
    g->transport->write(g->transport->ctx, frame, 6);

    timeout_write = systick_ms_now();
    while (ms_elapsed(systick_ms_now(), timeout_write) <= g->genie_cmd_timeout) {
        int command_return = genie_do_events(g);
        if (command_return == GENIE_ACK) {
            return 1;
        }
        if (command_return == GENIE_NAK) {
            return 0;
        }
    }
    if (g->user_debugger_handler != NULL) {
        g->user_debugger_handler(g->user_ctx, "Write Object didn't receive any reply\r\n");
    }
    g->display_detect_timer = systick_ms_now() + GENIE_DISPLAY_TIMEOUT + 10000u;
    return -1;
}

unsigned int genie_write_short_to_int_led_digits(genie_display_t *g, uint8_t index, int16_t data)
{
    return (unsigned int)genie_write_object(g, GENIE_OBJ_ILED_DIGITS_L, index, (uint16_t)data);
}

unsigned int genie_write_float_to_int_led_digits(genie_display_t *g, uint8_t index, float data)
{
    uint32_t bits;
    uint16_t word_hi, word_lo;
    int retval;

    memcpy(&bits, &data, sizeof(bits));
    word_hi = (uint16_t)((bits >> 16) & 0xFFFFu);
    word_lo = (uint16_t)(bits & 0xFFFFu);

    retval = genie_write_object(g, GENIE_OBJ_ILED_DIGITS_H, index, word_hi);
    if (retval == 1) {
        return (unsigned int)retval;
    }
    return (unsigned int)genie_write_object(g, GENIE_OBJ_ILED_DIGITS_L, index, word_lo);
}

unsigned int genie_write_long_to_int_led_digits(genie_display_t *g, uint8_t index, int32_t data)
{
    uint32_t bits;
    uint16_t word_hi, word_lo;
    int retval;

    memcpy(&bits, &data, sizeof(bits));
    word_hi = (uint16_t)((bits >> 16) & 0xFFFFu);
    word_lo = (uint16_t)(bits & 0xFFFFu);

    retval = genie_write_object(g, GENIE_OBJ_ILED_DIGITS_H, index, word_hi);
    if (retval == 1) {
        return (unsigned int)retval;
    }
    return (unsigned int)genie_write_object(g, GENIE_OBJ_ILED_DIGITS_L, index, word_lo);
}

int genie_write_contrast(genie_display_t *g, uint8_t value)
{
    uint8_t frame[3];
    uint32_t timeout_write;

    if (!g->display_detected) {
        return -1;
    }
    g->pending_ack = 1;
    genie_build_write_contrast_frame(value, frame);
    g->transport->write(g->transport->ctx, frame, 3);

    timeout_write = systick_ms_now();
    while (ms_elapsed(systick_ms_now(), timeout_write) <= g->genie_cmd_timeout) {
        int command_return = genie_do_events(g);
        if (command_return == GENIE_ACK) {
            return 1;
        }
        if (command_return == GENIE_NAK) {
            return 0;
        }
    }
    if (g->user_debugger_handler != NULL) {
        g->user_debugger_handler(g->user_ctx, "Write Contrast didn't receive any reply\r\n");
    }
    g->display_detect_timer = systick_ms_now() + GENIE_DISPLAY_TIMEOUT + 10000u;
    return -1;
}

int genie_write_str(genie_display_t *g, uint8_t index, const char *string)
{
    uint8_t frame[259];
    int n;
    uint32_t timeout_write;

    if (!g->display_detected) {
        return -1;
    }
    if (strlen(string) > 255) {
        return -1;
    }

    g->pending_ack = 1;
    n = genie_build_write_str_frame(index, string, frame, sizeof(frame));
    if (n < 0) {
        return -1;
    }
    g->transport->write(g->transport->ctx, frame, (size_t)n);

    timeout_write = systick_ms_now();
    while (ms_elapsed(systick_ms_now(), timeout_write) <= g->genie_cmd_timeout) {
        int command_return = genie_do_events(g);
        if (command_return == GENIE_ACK) {
            return 1;
        }
        if (command_return == GENIE_NAK) {
            return 0;
        }
    }
    if (g->user_debugger_handler != NULL) {
        g->user_debugger_handler(g->user_ctx, "Write String didn't receive any reply\r\n");
    }
    return -1;
}

int genie_write_inh_label_default(genie_display_t *g, uint8_t index)
{
    return genie_write_object(g, GENIE_OBJ_ILABELB, index, 0xFFFFu);
}

int genie_write_inh_label(genie_display_t *g, uint8_t index, const char *string)
{
    uint8_t frame[259];
    int n;
    uint32_t timeout_write;

    if (!g->display_detected) {
        return -1;
    }
    if (strlen(string) > 255) {
        return -1;
    }

    g->pending_ack = 1;
    n = genie_build_write_inh_label_frame(index, string, frame, sizeof(frame));
    if (n < 0) {
        return -1;
    }
    g->transport->write(g->transport->ctx, frame, (size_t)n);

    timeout_write = systick_ms_now();
    while (ms_elapsed(systick_ms_now(), timeout_write) <= g->genie_cmd_timeout) {
        int command_return = genie_do_events(g);
        if (command_return == GENIE_ACK) {
            return 1;
        }
        if (command_return == GENIE_NAK) {
            return 0;
        }
    }
    if (g->user_debugger_handler != NULL) {
        g->user_debugger_handler(g->user_ctx, "Write String didn't receive any reply\r\n");
    }
    return -1;
}

int genie_do_events(genie_display_t *g)
{
    uint8_t rx_data[GENIE_FRAME_SIZE];
    uint32_t now;

    genie_ping(g);

    now = systick_ms_now();
    if (ms_elapsed(now, g->delayed_cycles) >= GENIE_DISPLAY_TIMEOUT) {
        g->display_detect_timer = now;
    }
    g->delayed_cycles = now;

    if (genie_online(g)) {
        if (ms_elapsed(systick_ms_now(), g->display_detect_timer) > GENIE_DISPLAY_TIMEOUT) {
            g->display_detect_timer = systick_ms_now();
            g->display_detected = 0;
            g->ping_request = 0;
            if (g->user_debugger_handler != NULL) {
                g->user_debugger_handler(g->user_ctx,
                    "Display was not responding for quite a while. Is it disconnected?\r\n");
            }
            enqueue_ping_event(g, GENIE_DISCONNECTED);
            g->current_form = -1;
            return -1;
        }
    }

    if (!g->display_detected) {
        g->pending_ack = 0;
        g->current_form = -1;
        g->display_uptime = 0;
    }

    if (g->transport->read_available(g->transport->ctx) > 0) {
        int b = g->transport->peek(g->transport->ctx);
        int i;

        if (!g->display_detected && (b == GENIEM_REPORT_BYTES || b == GENIEM_REPORT_DBYTES)) {
            b = 0xFF;
        }

        switch (b) {

        case GENIE_ACK:
            g->display_detect_timer = systick_ms_now();
            (void)g->transport->getc(g->transport->ctx);
            g->bad_byte_counter = 0;
            g->pending_ack = 0;
            g->nak_inj = 0;
            return GENIE_ACK;

        case GENIE_NAK:
            g->display_detect_timer = systick_ms_now();
            (void)g->transport->getc(g->transport->ctx);
            g->nak_inj++;
            while (g->transport->peek(g->transport->ctx) == GENIE_NAK) {
                (void)g->transport->getc(g->transport->ctx);
            }
            if (g->nak_inj >= 2) {
                uint8_t recovery = 0xFF;
                g->nak_inj = 0;
                g->transport->write(g->transport->ctx, &recovery, 1);
            }
            g->pending_ack = 0;
            return GENIE_NAK;

        case GENIEM_REPORT_BYTES:
            if (g->transport->read_available(g->transport->ctx) < 3) {
                break;
            }
            rx_data[0] = (uint8_t)g->transport->getc(g->transport->ctx);
            rx_data[1] = (uint8_t)g->transport->getc(g->transport->ctx);
            rx_data[2] = (uint8_t)g->transport->getc(g->transport->ctx);
            g->display_detect_timer = systick_ms_now();
            g->bad_byte_counter = 0;
            if (g->user_magic_byte_handler != NULL) {
                g->user_magic_byte_handler(g->user_ctx, rx_data[1], rx_data[2]);
            } else {
                for (i = 0; i < rx_data[2]; i++) {
                    (void)g->transport->getc(g->transport->ctx);
                }
            }
            (void)genie_get_next_byte(g);
            return GENIEM_REPORT_BYTES;

        case GENIEM_REPORT_DBYTES:
            if (g->transport->read_available(g->transport->ctx) < 3) {
                break;
            }
            rx_data[0] = (uint8_t)g->transport->getc(g->transport->ctx);
            rx_data[1] = (uint8_t)g->transport->getc(g->transport->ctx);
            rx_data[2] = (uint8_t)g->transport->getc(g->transport->ctx);
            g->display_detect_timer = systick_ms_now();
            g->bad_byte_counter = 0;
            if (g->user_magic_dbyte_handler != NULL) {
                g->user_magic_dbyte_handler(g->user_ctx, rx_data[1], rx_data[2]);
            } else {
                for (i = 0; i < 2 * rx_data[2]; i++) {
                    (void)g->transport->getc(g->transport->ctx);
                }
            }
            (void)genie_get_next_byte(g);
            return GENIEM_REPORT_DBYTES;

        case GENIE_REPORT_EVENT: {
            genie_frame_t f;
            int checksum_ok;

            if (g->transport->read_available(g->transport->ctx) < 6) {
                break;
            }
            for (i = 0; i < 6; i++) {
                rx_data[i] = (uint8_t)g->transport->getc(g->transport->ctx);
            }
            checksum_ok = genie_parse_report_frame(rx_data, &f);
            if (!checksum_ok) {
                return 0;
            }
            g->display_detect_timer = systick_ms_now();
            g->bad_byte_counter = 0;
            if (f.object == GENIE_OBJ_FORM) {
                g->current_form = f.index;
            }
            genie_event_queue_enqueue(&g->event_queue, &f);
            return GENIE_REPORT_EVENT;
        }

        case GENIE_REPORT_OBJ: {
            genie_frame_t f;
            int checksum_ok;

            if (g->transport->read_available(g->transport->ctx) < 6) {
                break;
            }
            for (i = 0; i < 6; i++) {
                rx_data[i] = (uint8_t)g->transport->getc(g->transport->ctx);
            }
            checksum_ok = genie_parse_report_frame(rx_data, &f);
            if (!checksum_ok) {
                if (g->user_debugger_handler != NULL) {
                    g->user_debugger_handler(g->user_ctx, "Genie Report Object has bad CRC\r\n");
                }
                return 0;
            }
            g->display_detect_timer = systick_ms_now();
            g->bad_byte_counter = 0;
            if (f.object == GENIE_OBJ_FORM) {
                g->current_form = f.data_lsb;
                if (g->user_debugger_handler != NULL) {
                    g->user_debugger_handler(g->user_ctx, "Got Current Form\r\n");
                }
            }

            if ((g->auto_ping || g->ping_request) && f.object == GENIE_OBJ_FORM) {
                if (g->auto_ping) {
                    g->auto_ping = 0;
                    if (!g->display_detected) {
                        g->display_uptime = systick_ms_now();
                        enqueue_ping_event(g, GENIE_READY);
                        while (g->transport->read_available(g->transport->ctx) > 0) {
                            (void)g->transport->getc(g->transport->ctx);
                        }
                        g->display_detected = 1;
                    }
                    if (g->genie_start) {
                        g->genie_start = 0;
                        return GENIE_REPORT_OBJ;
                    }
                    break;
                }

                if (g->ping_request) {
                    g->ping_request = 0;
                    enqueue_ping_event(g, GENIE_ACK);
                }
                break;
            }

            genie_event_queue_enqueue(&g->event_queue, &f);
            return GENIE_REPORT_OBJ;
        }

        default:
            (void)g->transport->getc(g->transport->ctx);
            g->bad_byte_counter++;
            if (g->bad_byte_counter > 10) {
                g->bad_byte_counter = 0;
                if (g->user_debugger_handler != NULL) {
                    g->user_debugger_handler(g->user_ctx, "Bad bytes received. Manual disconnect\r\n");
                }
                g->display_detect_timer = systick_ms_now() + GENIE_DISPLAY_TIMEOUT + 10000u;
            }
            return GENIE_NAK;
        }
    }

    if (!g->pending_ack && g->event_queue.n_events > 0 && g->user_handler != NULL) {
        g->user_handler(g->user_ctx);
    }
    return 0;
}

void genie_attach_event_handler(genie_display_t *g, void *user_ctx, genie_user_handler_t handler)
{
    g->user_ctx = user_ctx;
    g->user_handler = handler;

    {
        genie_frame_t f;
        f.cmd = GENIE_PING;
        f.object = g->display_detected ? GENIE_READY : GENIE_DISCONNECTED;
        f.index = 0;
        f.data_msb = 0;
        f.data_lsb = 0;
        if (!g->display_detected && g->user_debugger_handler != NULL) {
            g->user_debugger_handler(g->user_ctx, "Display was disconnected while attaching event handler\r\n");
        }
        genie_event_queue_enqueue(&g->event_queue, &f);
    }
}

void genie_attach_magic_byte_reader(genie_display_t *g, void *user_ctx, genie_magic_byte_handler_t handler)
{
    g->user_ctx = user_ctx;
    g->user_magic_byte_handler = handler;
}

void genie_attach_magic_double_byte_reader(genie_display_t *g, void *user_ctx, genie_magic_dbyte_handler_t handler)
{
    g->user_ctx = user_ctx;
    g->user_magic_dbyte_handler = handler;
}

void genie_attach_debugger(genie_display_t *g, void *user_ctx, genie_debugger_handler_t handler)
{
    g->user_ctx = user_ctx;
    g->user_debugger_handler = handler;
}

int genie_online(const genie_display_t *g)
{
    return g->display_detected;
}

uint32_t genie_uptime(const genie_display_t *g)
{
    if (g->display_detected) {
        return ms_elapsed(systick_ms_now(), g->display_uptime);
    }
    return 0;
}

int genie_current_form(const genie_display_t *g)
{
    return g->current_form;
}

int genie_activate_form(genie_display_t *g, uint8_t form)
{
    return genie_write_object(g, GENIE_OBJ_FORM, form, 0);
}

void genie_recover(genie_display_t *g, uint8_t pulses)
{
    g->recover_pulse = pulses;
}

int genie_timeout(genie_display_t *g, uint32_t value)
{
    if (value < 50u) {
        return 0;
    }
    g->genie_cmd_timeout = value;
    return 1;
}

int genie_ping(genie_display_t *g)
{
    uint32_t ping_timer_interval;

    if (g->display_detected) {
        ping_timer_interval = GENIE_AUTO_PING_CYCLE;
    } else {
        ping_timer_interval = (uint32_t)g->recover_pulse;
    }

    if (ms_elapsed(systick_ms_now(), g->auto_ping_timer) > ping_timer_interval) {
        uint8_t frame[4];
        g->auto_ping_timer = systick_ms_now();
        g->auto_ping = 1;
        genie_build_read_obj_frame(GENIE_OBJ_FORM, 0, frame);
        g->transport->write(g->transport->ctx, frame, 4);
        if (g->user_debugger_handler != NULL) {
            g->user_debugger_handler(g->user_ctx, "Sending Read Form as Ping\r\n");
        }
    }
    return 1;
}

unsigned int genie_enable_auto_ping(genie_display_t *g, uint32_t interval)
{
    if (g->display_detected) {
        if (ms_elapsed(systick_ms_now(), g->ping_spacer) < interval) {
            return 0;
        }
        g->ping_spacer = systick_ms_now();
        g->ping_request = 1;

        {
            uint8_t frame[4];
            genie_build_read_obj_frame(GENIE_OBJ_FORM, 0, frame);
            g->transport->write(g->transport->ctx, frame, 4);
        }
        return 1;
    }

    if (ms_elapsed(systick_ms_now(), g->ping_spacer) > interval) {
        g->ping_spacer = systick_ms_now();
        enqueue_ping_event(g, GENIE_NAK);
    }
    return 0;
}

int genie_write_magic_bytes(genie_display_t *g, uint8_t index, const uint8_t *bytes, uint8_t len, int report)
{
    uint8_t frame[259];
    int n;
    uint32_t timeout_write;

    if (!g->display_detected) {
        return -1;
    }

    n = genie_build_magic_bytes_frame(index, bytes, len, frame);
    g->transport->write(g->transport->ctx, frame, (size_t)n);

    if (!report) {
        return 1;
    }
    g->pending_ack = 1;

    timeout_write = systick_ms_now();
    while (ms_elapsed(systick_ms_now(), timeout_write) <= g->genie_cmd_timeout) {
        int command_return = genie_do_events(g);
        if (command_return == GENIE_ACK) {
            return 1;
        }
        if (command_return == GENIE_NAK) {
            return 0;
        }
    }
    if (g->user_debugger_handler != NULL) {
        g->user_debugger_handler(g->user_ctx, "Write Magic Bytes didn't receive any reply\r\n");
    }
    g->display_detect_timer = systick_ms_now() + GENIE_DISPLAY_TIMEOUT + 10000u;
    return -1;
}

int genie_write_magic_dbytes(genie_display_t *g, uint8_t index, const uint16_t *shorts, uint8_t len, int report)
{
    uint8_t frame[259];
    int n;
    uint32_t timeout_write;

    if (!g->display_detected) {
        return -1;
    }

    n = genie_build_magic_dbytes_frame(index, shorts, len, frame);
    g->transport->write(g->transport->ctx, frame, (size_t)n);

    if (!report) {
        return 1;
    }
    g->pending_ack = 1;

    timeout_write = systick_ms_now();
    while (ms_elapsed(systick_ms_now(), timeout_write) <= g->genie_cmd_timeout) {
        int command_return = genie_do_events(g);
        if (command_return == GENIE_ACK) {
            return 1;
        }
        if (command_return == GENIE_NAK) {
            return 0;
        }
    }
    if (g->user_debugger_handler != NULL) {
        g->user_debugger_handler(g->user_ctx, "Write Magic Double Bytes didn't receive any reply\r\n");
    }
    g->display_detect_timer = systick_ms_now() + GENIE_DISPLAY_TIMEOUT + 10000u;
    return -1;
}

int genie_get_next_byte(genie_display_t *g)
{
    uint32_t timeout;

    if (!g->display_detected) {
        return -1;
    }

    timeout = systick_ms_now();
    while (g->transport->read_available(g->transport->ctx) < 1) {
        g->delayed_cycles = systick_ms_now();
        g->display_detect_timer = systick_ms_now();
        if (ms_elapsed(systick_ms_now(), timeout) >= 2000u) {
            g->display_detect_timer = systick_ms_now();
            g->display_detected = 0;
            while (g->transport->read_available(g->transport->ctx) > 0) {
                (void)g->transport->getc(g->transport->ctx);
            }
            if (g->user_debugger_handler != NULL) {
                g->user_debugger_handler(g->user_ctx, "Display was disconnected while waiting for next byte\r\n");
            }
            enqueue_ping_event(g, GENIE_DISCONNECTED);
            return 0;
        }
    }
    g->delayed_cycles = systick_ms_now();
    g->display_detect_timer = systick_ms_now();
    return g->transport->getc(g->transport->ctx);
}

int genie_get_next_double_byte(genie_display_t *g)
{
    uint32_t timeout;
    int hi, lo;

    if (!g->display_detected) {
        return -1;
    }

    timeout = systick_ms_now();
    while (g->transport->read_available(g->transport->ctx) < 2) {
        g->delayed_cycles = systick_ms_now();
        g->display_detect_timer = systick_ms_now();
        if (ms_elapsed(systick_ms_now(), timeout) >= 2000u) {
            g->display_detect_timer = systick_ms_now();
            g->display_detected = 0;
            while (g->transport->read_available(g->transport->ctx) > 0) {
                (void)g->transport->getc(g->transport->ctx);
            }
            if (g->user_debugger_handler != NULL) {
                g->user_debugger_handler(g->user_ctx, "Display was disconnected while waiting for next two bytes\r\n");
            }
            enqueue_ping_event(g, GENIE_DISCONNECTED);
            return -1;
        }
    }
    g->delayed_cycles = systick_ms_now();
    g->display_detect_timer = systick_ms_now();
    hi = g->transport->getc(g->transport->ctx);
    lo = g->transport->getc(g->transport->ctx);
    return (hi << 8) | lo;
}

int genie_dequeue_event(genie_display_t *g, genie_frame_t *out)
{
    return genie_event_queue_dequeue(&g->event_queue, out);
}
