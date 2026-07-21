#include "uhf_reader.h"
#include <string.h>
#include <stdio.h>

/* Debug tracing, added 2026-07-17 per explicit request ("reader is not
 * reading so it fails startup init... add some reader debug info") --
 * same pattern already used in neo_m8t_reader.c for the equivalent GPS
 * investigation: redirect PRINTF to debug_printf (LPUART5, independent
 * of the SWD/semihosting debug link -- see debug_console_rt1062.h's
 * own header comment) rather than pull in the toolchain's real printf.
 * Traces each step of uhf_reader_open()/uhf_reader_initialise()/
 * uhf_reader_set_antennae() so a "fails startup init" report can be
 * pinned to the exact command that stopped getting a reply, rather
 * than just the overall pass/fail this module previously exposed. */
#include "debug_console_rt1062.h"
#undef PRINTF
/* SILENCED 2026-07-21, per explicit request ("printf on ethernet
 * comms only after boot"). Was `debug_printf`. Restore if this
 * tracing is wanted again. */
#define PRINTF(...) ((void)0)

#define UHF_UART_BAUD 115200u

/* ------------------------------------------------------------------ */
/* Event handling for replies parsed during orchestration              */
/* ------------------------------------------------------------------ */

typedef struct {
    /* The two antenna checks are captured into SEPARATE masks so the
     * enable decision (in uhf_reader_set_antennae) can combine them
     * explicitly: a port is turned on iff its DC test OR its
     * return-loss test passed -- i.e. only if BOTH fail is it left off.
     * Keeping them apart (rather than OR-ing straight into r->ants as
     * frames arrive) makes that rule explicit in one place and immune
     * to frame ordering: a DC status frame can no longer overwrite an
     * already-confirmed return-loss result. */
    uint8_t dc_mask;    /* DC-connection check result (0x61 status frame) */
    uint8_t rl_mask;    /* return-loss/VSWR passes, OR-ed together */
    int *out_temp;
    int temp_found;
} reader_cb_ctx_t;

static void reader_event_cb(void *ctx, const uhf_frame_event_t *ev)
{
    reader_cb_ctx_t *c = (reader_cb_ctx_t *)ctx;
    if (ev->type == UHF_FRAME_ANT_STATUS) {
        c->dc_mask = ev->data.ant_status_mask;
    } else if (ev->type == UHF_FRAME_RETURN_LOSS && ev->data.return_loss.good) {
        c->rl_mask = (uint8_t)(c->rl_mask | ev->data.return_loss.ant_bit);
    } else if (ev->type == UHF_FRAME_TEMPERATURE) {
        if (c->out_temp != NULL) {
            *c->out_temp = ev->data.temperature;
        }
        c->temp_found = 1;
    }
}

/* ------------------------------------------------------------------ */

/* step_name is a short label for the debug trace only (e.g. "open:
 * get_version", "initialise: get_program") -- has no effect on
 * behavior. */
static int send_and_wait(const char *step_name, uhf_reader_t *r, const uint8_t *cmd, size_t len,
                          uint32_t delay_ms, uint32_t read_timeout_ms,
                          uint8_t *reply_buf, size_t reply_buf_size,
                          size_t *out_reply_len)
{
    int wr, n;

    wr = r->transport->write(r->transport->ctx, cmd, len);
    if (wr < 0) {
        PRINTF("UHF %s: write FAILED\r\n", step_name);
        return -1;
    }
    if (delay_ms > 0) {
        r->transport->delay_ms(r->transport->ctx, delay_ms);
    }
    n = r->transport->read(r->transport->ctx, reply_buf, reply_buf_size, read_timeout_ms);
    if (n < 0) {
        PRINTF("UHF %s: read FAILED\r\n", step_name);
        return -1;
    }
    PRINTF("UHF %s: OK, %d reply byte%s\r\n", step_name, n, n == 1 ? "" : "s");
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
        PRINTF("UHF open: transport open FAILED\r\n");
        return -1;
    }
    t->flush_rx(t->ctx);
    t->flush_tx(t->ctx);

    n = uhf_build_get_version(cmd, sizeof(cmd));
    if (n == 0) {
        return -1;
    }
    /* was: serEwrite(buf,5); msDelay(200); TM_ReadSerialPort(10); */
    return send_and_wait("open: get_version", r, cmd, n, 200, 10, reply, sizeof(reply), NULL);
}

int uhf_reader_set_antennae(uhf_reader_t *r, uhf_region_t region)
{
    uint8_t cmd[64];
    uint8_t reply[1024];
    size_t n, reply_len;
    int ant;
    reader_cb_ctx_t cb_ctx;

    cb_ctx.dc_mask = 0;
    cb_ctx.rl_mask = 0;
    cb_ctx.out_temp = NULL;
    cb_ctx.temp_found = 0;

    /* DC-connection check -- was `serEwrite(buf,6); msDelay(50);
     * TM_ReadSerialPort(10);`. The reply IS an antenna-status frame
     * (same 0x61 format uhf_parse_ant_status expects), so we parse it
     * directly here rather than relying on it surfacing later through
     * a separate receive path, which is a slightly more explicit
     * integration point than the original's implicit one.
     *
     * The DC check and the return-loss test below are two INDEPENDENT
     * ways to confirm an antenna, captured into cb_ctx.dc_mask and
     * cb_ctx.rl_mask respectively (both use the same bit order, antenna
     * 1 = bit 3 ... antenna 4 = bit 0) and combined by the explicit
     * OR near the end of this function. So a transport read failure
     * here must NOT abort: it just leaves dc_mask at 0 and lets the
     * VSWR test still confirm whatever it can. This also restores the
     * original TM_ReadSerialPort's behavior -- it was `void` and simply
     * processed nothing on an empty/failed read (`if(n>0)
     * TM_ProcessString`), never bailing out of TM_SetAntennae. Only a
     * command-build failure (n == 0, a programming error) is fatal. */
    n = uhf_build_dc_check(cmd, sizeof(cmd));
    if (n == 0) {
        return -1;
    }
    if (send_and_wait("set_antennae: dc_check", r, cmd, n, 50, 10, reply, sizeof(reply), &reply_len) == 0) {
        uhf_process_buffer(reply, reply_len, reader_event_cb, &cb_ctx);
    }
    PRINTF("UHF set_antennae: dc_mask=0x%02X\r\n", cb_ctx.dc_mask);

    /* Return-loss/VSWR test, only if the DC check didn't already show
     * all 4 antennas connected -- was `if(ants != 0x0F)`. Runs for
     * every antenna (the original's per-antenna `if(!(ants&k))` skip is
     * commented out), so an antenna the DC check missed can still be
     * confirmed here. */
    if (cb_ctx.dc_mask != 0x0F) {
        for (ant = 1; ant <= 4; ant++) {
            char step[32];
            n = uhf_build_return_loss_test(cmd, sizeof(cmd), region, (uint8_t)ant);
            if (n == 0) {
                return -1;
            }
            /* was: serEwrite(buf,27); msDelay(100); TM_ReadSerialPort(200); */
            /* Per-antenna read failure is non-fatal for the same
             * "either check confirms" reason: a failed VSWR read on one
             * port must not prevent the other ports (or the DC result)
             * from confirming a connection -- matching the original's
             * non-aborting read. */
            snprintf(step, sizeof(step), "set_antennae: rl_test ant%d", ant);
            if (send_and_wait(step, r, cmd, n, 100, 200, reply, sizeof(reply), &reply_len) == 0) {
                uhf_process_buffer(reply, reply_len, reader_event_cb, &cb_ctx);
            }
        }
    }
    PRINTF("UHF set_antennae: rl_mask=0x%02X\r\n", cb_ctx.rl_mask);

    /* THE ENABLE RULE, stated explicitly (per requirement): a port is
     * turned on iff its DC test OR its return-loss test passed. A port
     * that fails BOTH has its bit clear in both masks, so it is absent
     * from the union below and therefore absent from the antenna-enable
     * command -- it never gets turned on. This is the union the original
     * built implicitly by having DC assign `ants` and each good return
     * loss OR into it; made a single, order-independent statement here. */
    r->ants = (uint8_t)(cb_ctx.dc_mask | cb_ctx.rl_mask);
    PRINTF("UHF set_antennae: final ants=0x%02X\r\n", r->ants);

    /* Antenna-enable -- was the `if(ants){ ... }` block */
    n = uhf_build_antenna_enable(cmd, sizeof(cmd), r->ants, &r->duty_cycle);
    if (n > 0) {
        /* was: serEwrite(buf,buf[1]+5); msDelay(50); TM_ReadSerialPort(10); */
        if (send_and_wait("set_antennae: antenna_enable", r, cmd, n, 50, 10, reply, sizeof(reply), &reply_len) != 0) {
            return -1;
        }
    }

    /* Power-set -- was the fixed 31.5dBm command at the end of
     * TM_SetAntennae, the original's real default. CHANGED to 20dBm
     * 2026-07-17, per explicit instruction -- this test board's PSU is
     * power-limited (see UHF_POWER_20DBM's own doc comment, and
     * project memory's "user confirmed the PSU is fine running the
     * reader at the already-sent 15dBm override" from an earlier
     * standalone bring-up session) and 31.5dBm is a strong candidate
     * for a brownout-triggered reset the first time the reader keys up
     * RF for a real read, matching the reported "starts, beeps on a
     * chip read, then complete crash and reset" symptom exactly. A
     * deliberate deviation from the original's power level, not a
     * fidelity guess -- revert to UHF_POWER_31_5DBM once running from
     * a supply confirmed adequate for full power. */
    n = uhf_build_power_set(cmd, sizeof(cmd), UHF_POWER_20DBM);
    if (n == 0) {
        return -1;
    }
    /* was: serEwrite(buf,26); msDelay(50); TM_ReadSerialPort(10); */
    return send_and_wait("set_antennae: power_set", r, cmd, n, 50, 10, reply, sizeof(reply), &reply_len);
}

int uhf_reader_initialise(uhf_reader_t *r, uhf_region_t region,
                           uint8_t channel, int uhf_mode)
{
    uint8_t cmd[64];
    uint8_t reply[1024];
    size_t n, reply_len;
    size_t lens[3];
    int count, i, mode_n;
    size_t offset;

    /* Chips[] reset (was `memset(Chips, 0x00, sizeof(Chips))`) is the
     * caller's responsibility -- the chip array lives in
     * uhf_chip_array.h/.c, not owned by this orchestration module. */

    n = uhf_build_get_program(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait("initialise: get_program", r, cmd, n, 100, 10, reply, sizeof(reply), &reply_len) != 0) {
        PRINTF("UHF initialise: FAILED at get_program\r\n");
        return -1;
    }

    n = uhf_build_firmware_boot_mode(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait("initialise: firmware_boot_mode", r, cmd, n, 100, 10, reply, sizeof(reply), &reply_len) != 0) {
        PRINTF("UHF initialise: FAILED at firmware_boot_mode\r\n");
        return -1;
    }

    n = uhf_build_get_regions(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait("initialise: get_regions", r, cmd, n, 10, 10, reply, sizeof(reply), &reply_len) != 0) {
        PRINTF("UHF initialise: FAILED at get_regions\r\n");
        return -1;
    }

    n = uhf_build_region_frequency(cmd, sizeof(cmd), region, channel);
    if (n == 0) {
        return -1;
    }
    if (r->transport->write(r->transport->ctx, cmd, n) < 0) {
        PRINTF("UHF initialise: FAILED at region_frequency (write)\r\n");
        return -1;
    }
    /* FLAGGED ASYMMETRY -- see header comment. Only EU and AU wait for
     * a reply here; other regions send and move straight on. */
    if (region == UHF_REGION_EU) {
        r->transport->delay_ms(r->transport->ctx, 50);
        mode_n = r->transport->read(r->transport->ctx, reply, sizeof(reply), 10);
        PRINTF("UHF initialise: region_frequency (EU) reply=%d bytes\r\n", mode_n);
    } else if (region == UHF_REGION_AU) {
        r->transport->delay_ms(r->transport->ctx, 10);
        mode_n = r->transport->read(r->transport->ctx, reply, sizeof(reply), 10);
        PRINTF("UHF initialise: region_frequency (AU) reply=%d bytes\r\n", mode_n);
    } else {
        PRINTF("UHF initialise: region_frequency sent, no reply expected for this region\r\n");
    }

    n = uhf_build_set_dynamic_q(cmd, sizeof(cmd));
    if (n == 0 || send_and_wait("initialise: set_dynamic_q", r, cmd, n, 10, 10, reply, sizeof(reply), &reply_len) != 0) {
        PRINTF("UHF initialise: FAILED at set_dynamic_q\r\n");
        return -1;
    }

    n = uhf_build_mode_sequence(cmd, sizeof(cmd), uhf_mode, lens, &count);
    if (n == 0) {
        return -1;
    }
    offset = 0;
    for (i = 0; i < count; i++) {
        if (r->transport->write(r->transport->ctx, cmd + offset, lens[i]) < 0) {
            PRINTF("UHF initialise: FAILED at mode_sequence[%d] (write)\r\n", i);
            return -1;
        }
        r->transport->delay_ms(r->transport->ctx, 10);
        mode_n = r->transport->read(r->transport->ctx, reply, sizeof(reply), 10);
        PRINTF("UHF initialise: mode_sequence[%d] reply=%d bytes\r\n", i, mode_n);
        offset += lens[i];
    }

    PRINTF("UHF initialise: config sequence complete, running antenna check\r\n");
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
        PRINTF("UHF start: no antennas confirmed (ants=0), not starting\r\n");
        return 0;
    }

    n = uhf_build_start_reading(cmd, sizeof(cmd), heartbeat_enabled);
    if (n == 0) {
        return -1;
    }
    if (r->transport->write(r->transport->ctx, cmd, n) < 0) {
        PRINTF("UHF start: write FAILED\r\n");
        return -1;
    }
    PRINTF("UHF start: start command sent, ants=0x%02X\r\n", r->ants);
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
    if (send_and_wait("stop: stop_reading", r, cmd, n, 10, 1, reply, sizeof(reply), NULL) != 0) {
        return -1;
    }

    /* Was the second command in StopReaders(): query temperature.
     * DISABLED 2026-07-17, per explicit report: the reader doesn't
     * reliably answer a temperature query immediately after stop_reading
     * -- only when genuinely idle, not just-stopped -- so this was
     * failing here in practice. Not currently used by any caller
     * (uhf_reader_get_temperature() is unused now), so commented out
     * rather than chasing the exact idle-settle timing. The function
     * itself is left intact/callable if a real use for it comes up
     * later (e.g. queried explicitly while confirmed idle, not as part
     * of every stop).
     * if (uhf_reader_get_temperature(r, NULL) < 0) {
     *     return -1;
     * }
     */

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
    if (send_and_wait("get_temperature", r, cmd, n, 0, 10, reply, sizeof(reply), &reply_len) != 0) {
        return -1;
    }

    cb_ctx.dc_mask = 0;
    cb_ctx.rl_mask = 0;
    cb_ctx.out_temp = out_temp;
    cb_ctx.temp_found = 0;
    uhf_process_buffer(reply, reply_len, reader_event_cb, &cb_ctx);

    return cb_ctx.temp_found ? 1 : 0;
}
