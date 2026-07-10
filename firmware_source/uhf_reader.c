#include "uhf_reader.h"
#include <string.h>

#define UHF_UART_BAUD 115200u

/* ------------------------------------------------------------------ */
/* Event handling for replies parsed during orchestration              */
/* ------------------------------------------------------------------ */

typedef struct {
    uhf_reader_t *r;
    int *out_temp;
    int temp_found;
} reader_cb_ctx_t;

static void reader_event_cb(void *ctx, const uhf_frame_event_t *ev)
{
    reader_cb_ctx_t *c = (reader_cb_ctx_t *)ctx;
    if (ev->type == UHF_FRAME_ANT_STATUS) {
        c->r->ants = ev->data.ant_status_mask;
    } else if (ev->type == UHF_FRAME_RETURN_LOSS && ev->data.return_loss.good) {
        c->r->ants = (uint8_t)(c->r->ants | ev->data.return_loss.ant_bit);
    } else if (ev->type == UHF_FRAME_TEMPERATURE) {
        if (c->out_temp != NULL) {
            *c->out_temp = ev->data.temperature;
        }
        c->temp_found = 1;
    }
}

/* ------------------------------------------------------------------ */

static int send_and_wait(uhf_reader_t *r, const uint8_t *cmd, size_t len,
                          uint32_t delay_ms, uint32_t read_timeout_ms,
                          uint8_t *reply_buf, size_t reply_buf_size,
                          size_t *out_reply_len)
{
    int wr, n;

    wr = r->transport->write(r->transport->ctx, cmd, len);
    if (wr < 0) {
        return -1;
    }
    if (delay_ms > 0) {
        r->transport->delay_ms(r->transport->ctx, delay_ms);
    }
    n = r->transport->read(r->transport->ctx, reply_buf, reply_buf_size, read_timeout_ms);
    if (n < 0) {
        return -1;
    }
    if (out_reply_len != NULL) {
        *out_reply_len = (size_t)n;
    }
    return 0;
}

int uhf_reader_open(uhf_reader_t *r, const uhf_transport_t *t)
{
    uint8_t cmd[8];
    uint8_t reply[1024];
    size_t n;

    memset(r, 0, sizeof(*r));
    r->transport = t;

    if (t->open(t->ctx, UHF_UART_BAUD) != 0) {
        return -1;
    }
    t->flush_rx(t->ctx);
    t->flush_tx(t->ctx);

    n = uhf_build_get_version(cmd, sizeof(cmd));
    if (n == 0) {
        return -1;
    }
    /* was: serEwrite(buf,5); msDelay(200); TM_ReadSerialPort(10); */
    return send_and_wait(r, cmd, n, 200, 10, reply, sizeof(reply), NULL);
}

int uhf_reader_set_antennae(uhf_reader_t *r, uhf_region_t region)
{
    uint8_t cmd[64];
    uint8_t reply[1024];
    size_t n, reply_len;
    int ant;
    reader_cb_ctx_t cb_ctx;

    cb_ctx.r = r;
    cb_ctx.out_temp = NULL;
    cb_ctx.temp_found = 0;

    /* DC-connection check -- was `serEwrite(buf,6); msDelay(50);
     * TM_ReadSerialPort(10);`. The reply IS an antenna-status frame
     * (same 0x61 format uhf_parse_ant_status expects), so we parse it
     * directly here rather than relying on it surfacing later through
     * a separate receive path, which is a slightly more explicit
     * integration point than the original's implicit one. */
    n = uhf_build_dc_check(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait(r, cmd, n, 50, 10, reply, sizeof(reply), &reply_len) != 0) {
        return -1;
    }
    uhf_process_buffer(reply, reply_len, reader_event_cb, &cb_ctx);

    /* Return-loss/VSWR test, only if DC check didn't already show all
     * 4 antennas connected -- was `if(ants != 0x0F)`. */
    if (r->ants != 0x0F) {
        for (ant = 1; ant <= 4; ant++) {
            n = uhf_build_return_loss_test(cmd, sizeof(cmd), region, (uint8_t)ant);
            if (n == 0) {
                return -1;
            }
            /* was: serEwrite(buf,27); msDelay(100); TM_ReadSerialPort(200); */
            if (send_and_wait(r, cmd, n, 100, 200, reply, sizeof(reply), &reply_len) != 0) {
                return -1;
            }
            uhf_process_buffer(reply, reply_len, reader_event_cb, &cb_ctx);
        }
    }

    /* Antenna-enable -- was the `if(ants){ ... }` block */
    n = uhf_build_antenna_enable(cmd, sizeof(cmd), r->ants, &r->duty_cycle);
    if (n > 0) {
        /* was: serEwrite(buf,buf[1]+5); msDelay(50); TM_ReadSerialPort(10); */
        if (send_and_wait(r, cmd, n, 50, 10, reply, sizeof(reply), &reply_len) != 0) {
            return -1;
        }
    }

    /* Power-set -- was the fixed 31.5dBm command at the end of TM_SetAntennae */
    n = uhf_build_power_set(cmd, sizeof(cmd), UHF_POWER_31_5DBM);
    if (n == 0) {
        return -1;
    }
    /* was: serEwrite(buf,26); msDelay(50); TM_ReadSerialPort(10); */
    return send_and_wait(r, cmd, n, 50, 10, reply, sizeof(reply), &reply_len);
}

int uhf_reader_initialise(uhf_reader_t *r, uhf_region_t region,
                           uint8_t channel, int uhf_mode)
{
    uint8_t cmd[64];
    uint8_t reply[1024];
    size_t n, reply_len;
    size_t lens[3];
    int count, i;
    size_t offset;

    /* Chips[] reset (was `memset(Chips, 0x00, sizeof(Chips))`) is the
     * caller's responsibility -- the chip array lives in
     * uhf_chip_array.h/.c, not owned by this orchestration module. */

    n = uhf_build_get_program(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait(r, cmd, n, 100, 10, reply, sizeof(reply), &reply_len) != 0) {
        return -1;
    }

    n = uhf_build_firmware_boot_mode(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait(r, cmd, n, 100, 10, reply, sizeof(reply), &reply_len) != 0) {
        return -1;
    }

    n = uhf_build_get_regions(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait(r, cmd, n, 10, 10, reply, sizeof(reply), &reply_len) != 0) {
        return -1;
    }

    n = uhf_build_region_frequency(cmd, sizeof(cmd), region, channel);
    if (n == 0) {
        return -1;
    }
    if (r->transport->write(r->transport->ctx, cmd, n) < 0) {
        return -1;
    }
    /* FLAGGED ASYMMETRY -- see header comment. Only EU and AU wait for
     * a reply here; other regions send and move straight on. */
    if (region == UHF_REGION_EU) {
        r->transport->delay_ms(r->transport->ctx, 50);
        r->transport->read(r->transport->ctx, reply, sizeof(reply), 10);
    } else if (region == UHF_REGION_AU) {
        r->transport->delay_ms(r->transport->ctx, 10);
        r->transport->read(r->transport->ctx, reply, sizeof(reply), 10);
    }

    n = uhf_build_set_dynamic_q(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait(r, cmd, n, 10, 10, reply, sizeof(reply), &reply_len) != 0) {
        return -1;
    }

    n = uhf_build_mode_sequence(cmd, sizeof(cmd), uhf_mode, lens, &count);
    if (n == 0) {
        return -1;
    }
    offset = 0;
    for (i = 0; i < count; i++) {
        if (r->transport->write(r->transport->ctx, cmd + offset, lens[i]) < 0) {
            return -1;
        }
        r->transport->delay_ms(r->transport->ctx, 10);
        r->transport->read(r->transport->ctx, reply, sizeof(reply), 10);
        offset += lens[i];
    }

    return uhf_reader_set_antennae(r, region);
}

int uhf_reader_start(uhf_reader_t *r, int heartbeat_enabled)
{
    uint8_t cmd[32];
    size_t n;

    if (r->ants == 0) {
        /* Matches the original: no antenna connected, abort silently
         * (the caller is expected to also flip ProgramState back to
         * IDLE and update the display -- that's app-layer UI state,
         * not this module's concern). */
        return 0;
    }

    n = uhf_build_start_reading(cmd, sizeof(cmd), heartbeat_enabled);
    if (n == 0) {
        return -1;
    }
    if (r->transport->write(r->transport->ctx, cmd, n) < 0) {
        return -1;
    }
    /* The original also switches the cooling fan on here
     * (BitWrPortI(PEDR,...,1,5)) -- that's board-level GPIO outside
     * this reader-protocol module's scope; do it alongside this call
     * in your integration code. */
    return 1;
}

int uhf_reader_stop(uhf_reader_t *r)
{
    uint8_t cmd[32];
    uint8_t reply[256];
    size_t n;

    n = uhf_build_stop_reading(cmd, sizeof(cmd));
    if (n == 0) {
        return -1;
    }
    /* was: serEwrite(buf,19); msDelay(10); TM_ReadSerialPort(1); */
    if (send_and_wait(r, cmd, n, 10, 1, reply, sizeof(reply), NULL) != 0) {
        return -1;
    }

    /* was the second command in StopReaders(): query temperature */
    if (uhf_reader_get_temperature(r, NULL) < 0) {
        return -1;
    }

    /* Fan-off GPIO is, again, the caller's concern -- see uhf_reader_start(). */
    return 0;
}

int uhf_reader_get_temperature(uhf_reader_t *r, int *out_temp)
{
    uint8_t cmd[16];
    uint8_t reply[256];
    size_t n, reply_len;
    reader_cb_ctx_t cb_ctx;

    n = uhf_build_get_temperature(cmd, sizeof(cmd));
    if (n == 0) {
        return -1;
    }
    if (send_and_wait(r, cmd, n, 0, 10, reply, sizeof(reply), &reply_len) != 0) {
        return -1;
    }

    cb_ctx.r = r;
    cb_ctx.out_temp = out_temp;
    cb_ctx.temp_found = 0;
    uhf_process_buffer(reply, reply_len, reader_event_cb, &cb_ctx);

    return cb_ctx.temp_found ? 1 : 0;
}
