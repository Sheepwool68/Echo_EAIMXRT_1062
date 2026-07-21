/*
 * neo_m8t_reader.c
 *
 * ==========================================================================
 * SCAFFOLD (process_ubx_message/configure_timepulse/poll_pvt/update_signal_status)
 * -- I/O choreography, same honest scope note as gprs_modem.c/uhf_reader.c.
 * gps_sync_time_from_fix_impl is pure enough (once given a pvt fix and tick
 * count) to be fully unit tested via a mock transport -- see test_neo_m8t_reader.c.
 * ==========================================================================
 */

#include "neo_m8t_reader.h"
#include "display_stub.h"
#include "bringup_config.h"
#include "ms_time.h"
#include <string.h>

/* TEMPORARY DIAGNOSTIC, added 2026-07-16 -- REMOVE once the "no time
 * fix" root cause is confirmed. Same pattern already used successfully
 * once on this exact GPS module earlier in the project (a raw SPI dump
 * that revealed genuine NMEA sentences coming back, see project
 * memory) -- neo_m8t_gps_get_pps_state() confirms PPS toggling but
 * neo_m8t_poll_pvt() never succeeds, so this dumps what
 * process_ubx_message() actually sees on a timeout, to distinguish
 * "GPS never replies at all" from "GPS replies with something this
 * parser doesn't recognize as a UBX-NAV-PVT frame" (e.g. NMEA text
 * still enabled despite ubx_prt's CFG-PRT write, which is sent but
 * whose ACK result nothing has ever checked, in the original OR this
 * port). Not gated behind any bringup_config.h flag on purpose --
 * this file doesn't currently include that machinery, and this is
 * meant to come out again shortly regardless of flag state. */
#include "debug_console_rt1062.h"
#undef PRINTF
/* SILENCED 2026-07-21, per explicit request ("printf on ethernet
 * comms only after boot"). Was `debug_printf`. Restore if this
 * tracing is wanted again. */
#define PRINTF(...) ((void)0)

#define UBX_RESPONSE_SIZE 2000u

static void do_transfer(const neo_m8t_transport_t *t, uint8_t *command, size_t length, uint8_t *response)
{
    t->cs_enable(t->ctx);
    t->delay_ms(t->ctx, 1);
    t->transfer(t->ctx, command, length, response, UBX_RESPONSE_SIZE);
    t->cs_disable(t->ctx);
    t->delay_ms(t->ctx, 1);
}

static void do_read_only(const neo_m8t_transport_t *t, uint8_t *response)
{
    t->cs_enable(t->ctx);
    t->delay_ms(t->ctx, 1);
    t->transfer(t->ctx, NULL, 0, response, UBX_RESPONSE_SIZE);
    t->cs_disable(t->ctx);
    t->delay_ms(t->ctx, 1);
}

int process_ubx_message(const neo_m8t_transport_t *t, uint8_t *command, size_t length,
                         int message_type, neo_pvt_t *out_pvt, uint32_t (*now_ms_fn)(void))
{
    uint8_t response[UBX_RESPONSE_SIZE];
    uint32_t timeout_start;

    ubx_compute_checksum(command, length);
    memset(response, 0, sizeof(response));
    do_transfer(t, command, length, response);

    timeout_start = now_ms_fn();

    for (;;) {
        int offset = ubx_find_sync(response, sizeof(response));

        /* REMOVED 2026-07-16 -- was a per-attempt trace here (every
         * single retry within the 1100ms window, not just the final
         * one), restricted to message_type==1 (PVT polls). Already
         * confirmed everything it was added to check: the response is
         * consistently clean 0xFF filler on failure (not corrupted,
         * not NMEA text), never varies in a way that changes the
         * diagnosis. Its own doc comment already flagged the real cost
         * of keeping it: each PRINTF over LPUART5 measurably slows that
         * retry, so keeping this active reduces how many actual SPI
         * attempts fit inside the fixed 1100ms window -- directly
         * counterproductive now that the open question is "why is the
         * per-attempt success rate low," since this diagnostic was
         * itself making that rate lower. The one-shot timeout dump
         * below (fires once per FAILED message, not per attempt) still
         * gives the same "what did the last response look like"
         * visibility at a fraction of the cost. */

        if (offset >= 0) {
            if (message_type) {
                if ((size_t)offset + 38 <= sizeof(response) && ubx_is_nav_pvt(&response[(size_t)offset])) {
                    ubx_parse_nav_pvt(&response[(size_t)offset], out_pvt);
                    return 1;
                }
            } else {
                if ((size_t)offset + 4 <= sizeof(response)) {
                    int ack = ubx_classify_ack(&response[(size_t)offset]);
                    if (ack == 1) {
                        return 1;
                    }
                    if (ack == 0) {
                        return 0;
                    }
                }
            }
        }

        do_read_only(t, response);

        if (ms_has_elapsed(now_ms_fn(), timeout_start, 1100)) {
            /* TEMPORARY DIAGNOSTIC -- see file header. Dumps the
             * command's class/id and the first 48 bytes of the LAST
             * response snapshot as both hex and printable ASCII, to
             * tell apart "GPS never replies" (all 0xFF/0x00) from
             * "GPS replies with something not recognized as UBX"
             * (e.g. NMEA text like "$GNRMC" would show up directly in
             * the ASCII column). Was RESTRICTED to message_type==1
             * (same reason as the per-attempt trace above -- that
             * restriction was about the PER-ATTEMPT trace's overhead,
             * which fires up to ~60x per message and multiplied boot
             * time badly; this ONE-SHOT dump (fires once per message,
             * only on final timeout) never had that problem and is
             * re-enabled for all message types 2026-07-16, after the
             * CS-contention fix improved CFG-message ACKs from 0/6 to
             * 1/6 but left 5/6 still failing -- need to see what those
             * failures actually look like now (still all-FF, or
             * something else) to keep narrowing this down. */
            int k;
            PRINTF("UBX timeout: cmd class=0x%02X id=0x%02X type=%d, last response:\r\n",
                   command[2], command[3], message_type);
            PRINTF("  hex: ");
            for (k = 0; k < 48; k++) {
                PRINTF("%02X ", response[k]);
            }
            PRINTF("\r\n  asc: ");
            for (k = 0; k < 48; k++) {
                uint8_t c = response[k];
                PRINTF("%c", (c >= 32 && c < 127) ? (char)c : '.');
            }
            PRINTF("\r\n");
            return 0;
        }
    }
}

void neo_m8t_configure_timepulse(const neo_m8t_transport_t *t, uint32_t (*now_ms_fn)(void))
{
    static uint8_t ubx_pm2[56] = {
        0xB5,0x62,0x06,0x3B,0x30, 0x00,
        0x02, 0x06, 0x00, 0x00, 0x00, 0x14, 0x42, 0x01,
        0xE8, 0x03, 0x00, 0x00, 0x10, 0x27, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x2C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00};

    static uint8_t ubx_navx5[48] = {
        0xB5,0x62,0x06,0x23,0x28, 0x00,
        0x02, 0x00, 0xFF, 0xFF, 0x3F, 0x02, 0x00, 0x00,
        0x03, 0x02, 0x01, 0x20, 0x09, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x4B, 0x07, 0x00, 0x01, 0x00, 0x00,
        0x01, 0x01, 0x00, 0x00, 0x00, 0x64, 0x64, 0x00,
        0x00, 0x01, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00};

    static uint8_t ubx_nav5[44] = {
        0xB5,0x62,0x06,0x24,0x24,0x00,
        0xFF,0xFF,0x02, 0x03, 0x00, 0x00, 0x00, 0x00,
        0x10, 0x27,0x00, 0x00, 0x05, 0x00, 0xFA, 0x00,
        0xFA, 0x00, 0x64, 0x00, 0x5E, 0x01, 0x00, 0x3C,
        0x00, 0x00,0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    static uint8_t ubx_tp5[40] = {
        0xB5,0x62,0x06,0x31,0x20,0x00,
        0x00,0x01,0x00,0x00,0x32,0x00,0x00,0x00,
        0x40,0x42,0x0F,0x00,0x40,0x42,0x0F,0x00,
        0x00,0x00,0x00,0x00,0xA0,0x86,0x01,0x00,
        0x00,0x00,0x00,0x01,0xF7,0x00,0x00,0x00,
        0x00,0x00};

    static uint8_t ubx_gnss[68] = {
        0xB5,0x62,0x06,0x3E,0x3C,0x00,0x00,0x20,0x20,0x07,
        0x00,0x08,0x10,0x00,0x01,0x00,0x01,0x01,
        0x01,0x01,0x03,0x00,0x00,0x00,0x01,0x01,
        0x02,0x04,0x08,0x00,0x00,0x00,0x01,0x01,
        0x03,0x08,0x10,0x00,0x00,0x00,0x01,0x01,
        0x04,0x00,0x08,0x00,0x00,0x00,0x01,0x03,
        0x05,0x00,0x03,0x00,0x01,0x00,0x01,0x05,
        0x06,0x08,0x0E,0x00,0x00,0x00,0x01,0x01,
        0x00,0x00};

    static uint8_t ubx_prt[28] = {
        0xB5,0x62,0x06,0x00,0x14,0x00,
        0x04,0x00,0x00,0x00,0x00,0x32,0x00,0x00,
        0x00,0x00,0x00,0x00,0x01,0x00,0x01,
        0x00,0x00,0x00,0x00,0x00,0x5A,0xD0};

    /* REVERTED to single-attempt sends, 2026-07-16, per explicit
     * instruction ("get rid of all the ubx retries. it is slowing
     * things up") -- the bounded retry (send_cfg_with_retry(), 2
     * attempts) was a genuine attempt to improve boot-to-boot GPS
     * reliability, but its own worst-case added latency (each attempt
     * up to 1100ms, x6 messages) was judged not worth it. Back to one
     * attempt per message, matching this function's original
     * (pre-2026-07-16-retry-experiment) behavior. */
    neo_pvt_t unused_pvt;
    int r;

    /* GPS BUS-PRIMING READ REMOVED, 2026-07-17, per explicit instruction
     * ("get rid of it... the dynamic C code works - it is the
     * blueprint"). This was a speculative, non-source-backed addition
     * from 2026-07-16 -- the ORIGINAL source only ever primes the bus
     * ONCE, immediately before comms_NRF(0x0E) specifically (confirmed
     * by direct source reading, see nrf_spi_transport.h/app_init.c's own
     * priming block), because on the Rabbit board GPS is ALWAYS the
     * first SPI user of the whole boot -- GPS itself never needed
     * priming there, since nothing came before it. This port had
     * inherited that same "first transaction is unreliable" symptom and
     * guessed that generalizing the priming pattern to GPS's own first
     * message might help, without source backing. That guess is now
     * obsolete: the same day's later investigation (scope-verified
     * against the real Rabbit hardware) found and fixed the REAL causes
     * of unreliable SPI behavior on this bus -- wrong SPI mode, wrong CS
     * setup timing, wrong clock speed, and debug-probe/JTAG interference
     * during live debugging -- all now corrected at the source instead
     * of worked around. Removing this extra, unsourced CS2 assert/
     * deassert cycle (with its own 200ms delay not present in the
     * original) restores exact fidelity: GPS's first real transaction is
     * now `ubx_pm2` directly, with nothing before it, matching the
     * original exactly. */

    r = process_ubx_message(t, ubx_pm2, sizeof(ubx_pm2), 0, &unused_pvt, now_ms_fn);
    PRINTF("UBX cfg pm2: %s\r\n", r ? "ACKed" : "FAILED");
    r = process_ubx_message(t, ubx_navx5, sizeof(ubx_navx5), 0, &unused_pvt, now_ms_fn);
    PRINTF("UBX cfg navx5: %s\r\n", r ? "ACKed" : "FAILED");
    r = process_ubx_message(t, ubx_nav5, sizeof(ubx_nav5), 0, &unused_pvt, now_ms_fn);
    PRINTF("UBX cfg nav5: %s\r\n", r ? "ACKed" : "FAILED");
    r = process_ubx_message(t, ubx_tp5, sizeof(ubx_tp5), 0, &unused_pvt, now_ms_fn);
    PRINTF("UBX cfg tp5: %s\r\n", r ? "ACKed" : "FAILED");
    r = process_ubx_message(t, ubx_gnss, sizeof(ubx_gnss), 0, &unused_pvt, now_ms_fn);
    PRINTF("UBX cfg gnss: %s\r\n", r ? "ACKed" : "FAILED");
    r = process_ubx_message(t, ubx_prt, sizeof(ubx_prt), 0, &unused_pvt, now_ms_fn);
    PRINTF("UBX cfg prt: %s\r\n", r ? "ACKed" : "FAILED");

    /* ARCHITECTURE, 2026-07-18: an earlier attempt sent ubx_prt
     * standalone, much earlier in boot (before even the nRF's own
     * queries), at a guaranteed Mode 0, then switched the WHOLE bus to
     * Mode 1 for everything after (nRF queries + GPS's remaining CFG
     * messages). That got the nRF working (confirmed, `fw_version=0x07`,
     * clean power-cycle test) but did NOT help GPS -- its remaining
     * messages still failed completely at Mode 1, exactly as before,
     * proving GPS genuinely cannot tolerate Mode 1 on this specific
     * hardware (unlike the real Rabbit reference, which apparently can).
     * Reverted that approach. Current architecture: the bus's static
     * config (`LPSPI3_config`, `board/peripherals.c`) stays permanently
     * at Mode 0 -- GPS's true native/default mode, confirmed both by the
     * u-blox datasheet (SPI defaults to Mode 0) and empirically (GPS
     * only ever got real ACKs at Mode 0 all session). GPS's own code
     * (this function) never touches SPI mode at all -- it just always
     * finds the bus at Mode 0. The nRF52833's Mode-1 requirement is now
     * handled entirely inside its OWN transport layer
     * (nrf_spi_transport_rt1062.c) -- it switches into Mode 1
     * immediately before each of its own transfers and switches straight
     * back to Mode 0 right after, so GPS never sees anything but Mode 0
     * at any point in boot, and the nRF's Mode 1 need is fully
     * self-contained. ubx_prt is back in its original, source-faithful
     * last-in-sequence position (no more early-send/reordering). */
}

int neo_m8t_poll_pvt(const neo_m8t_transport_t *t, neo_pvt_t *out_pvt, uint32_t (*now_ms_fn)(void))
{
    uint8_t ubx_pvt[8] = {0xB5, 0x62, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00};
    return process_ubx_message(t, ubx_pvt, sizeof(ubx_pvt), 1, out_pvt, now_ms_fn);
}

int neo_m8t_update_signal_status(const neo_m8t_transport_t *t, uint32_t (*now_ms_fn)(void),
                                  int *out_raw_sats, int *out_status)
{
    neo_pvt_t pvt;
    int sats;

    if (!neo_m8t_poll_pvt(t, &pvt, now_ms_fn)) {
        if (out_raw_sats != NULL) {
            *out_raw_sats = -1;
        }
        if (out_status != NULL) {
            *out_status = -1;
        }
        return -1;
    }

    sats = pvt.sats * 7;
    if (sats > 100) {
        sats = 100;
    }

#if APP_ENABLE_DISPLAY
    display_set_tank(GENIE_TANK_GPS, sats);
    display_set_led(DISPLAY_LED_PPS, pvt.status);
#endif

    if (out_raw_sats != NULL) {
        *out_raw_sats = pvt.sats;
    }
    if (out_status != NULL) {
        *out_status = pvt.status;
    }

    return sats;
}

int gps_sync_time_from_fix_impl(const neo_m8t_transport_t *t, rtc_datetime_t *out_time,
                                 int time_zone_hours, int add30,
                                 uint32_t (*now_ms_fn)(void), uint32_t (*tick_count_fn)(void))
{
    neo_pvt_t pvt;
    uint32_t start_tick, timeout_start;
    int time_set = 0;

    if (!neo_m8t_poll_pvt(t, &pvt, now_ms_fn)) {
        return 0;
    }

    start_tick = tick_count_fn();
    timeout_start = now_ms_fn();

    for (;;) {
        if (ms_has_elapsed(now_ms_fn(), timeout_start, 2000)) {
            break;
        }

        if (tick_count_fn() > start_tick && pvt.status) {
            rtc_datetime_t raw;
            int64_t offset_seconds;
            int32_t extra_second = (int32_t)(tick_count_fn() - 1 - start_tick);

            raw.year = pvt.year;
            raw.mon = pvt.month;
            raw.mday = pvt.day;
            raw.hour = pvt.hours;
            raw.min = pvt.minutes;
            raw.sec = pvt.seconds;
            raw.wday = 0;

            offset_seconds = (int64_t)time_zone_hours * 3600 + (int64_t)add30 * 1800 + extra_second;
            *out_time = rtc_apply_seconds_offset(&raw, offset_seconds);

            time_set = 1;
            break;
        }
    }

    return time_set;
}
