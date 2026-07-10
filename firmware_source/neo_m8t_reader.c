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

    neo_pvt_t unused_pvt;

    process_ubx_message(t, ubx_pm2, sizeof(ubx_pm2), 0, &unused_pvt, now_ms_fn);
    process_ubx_message(t, ubx_navx5, sizeof(ubx_navx5), 0, &unused_pvt, now_ms_fn);
    process_ubx_message(t, ubx_nav5, sizeof(ubx_nav5), 0, &unused_pvt, now_ms_fn);
    process_ubx_message(t, ubx_tp5, sizeof(ubx_tp5), 0, &unused_pvt, now_ms_fn);
    process_ubx_message(t, ubx_gnss, sizeof(ubx_gnss), 0, &unused_pvt, now_ms_fn);
    process_ubx_message(t, ubx_prt, sizeof(ubx_prt), 0, &unused_pvt, now_ms_fn);
}

int neo_m8t_poll_pvt(const neo_m8t_transport_t *t, neo_pvt_t *out_pvt, uint32_t (*now_ms_fn)(void))
{
    uint8_t ubx_pvt[8] = {0xB5, 0x62, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00};
    return process_ubx_message(t, ubx_pvt, sizeof(ubx_pvt), 1, out_pvt, now_ms_fn);
}

int neo_m8t_update_signal_status(const neo_m8t_transport_t *t, uint32_t (*now_ms_fn)(void))
{
    neo_pvt_t pvt;
    int sats;

    if (!neo_m8t_poll_pvt(t, &pvt, now_ms_fn)) {
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
