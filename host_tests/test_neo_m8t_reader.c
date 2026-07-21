#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "neo_m8t_reader.h"

/* ---- Fake clock and tick counter, both test-controlled ---- */
static uint32_t g_fake_now;
static uint32_t g_fake_tick;
static uint32_t fake_now_ms(void) { return g_fake_now; }

/* ---- Mock SPI transport ---- */
typedef struct {
    uint8_t canned_response[2000]; /* what transfer() copies into rx */
    int have_canned_response;      /* whether to serve the canned response or all-zero */
    int transfer_calls;
    int cs_enable_calls, cs_disable_calls;
} mock_spi_t;

static void m_cs_enable(void *ctx) { ((mock_spi_t *)ctx)->cs_enable_calls++; }
static void m_cs_disable(void *ctx) { ((mock_spi_t *)ctx)->cs_disable_calls++; }
static void m_delay_ms(void *ctx, uint32_t ms) { (void)ctx; (void)ms; /* no real delay in tests */ }
static void m_transfer(void *ctx, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len)
{
    mock_spi_t *m = (mock_spi_t *)ctx;
    (void)tx; (void)tx_len;
    m->transfer_calls++;
    if (m->have_canned_response) {
        memcpy(rx, m->canned_response, rx_len < sizeof(m->canned_response) ? rx_len : sizeof(m->canned_response));
    } else {
        memset(rx, 0, rx_len);
    }
}

static neo_m8t_transport_t make_transport(mock_spi_t *m) {
    neo_m8t_transport_t t;
    t.ctx = m;
    t.cs_enable = m_cs_enable;
    t.cs_disable = m_cs_disable;
    t.delay_ms = m_delay_ms;
    t.transfer = m_transfer;
    return t;
}

static void fill_nav_pvt_response(uint8_t *buf, uint16_t year, uint8_t month, uint8_t day,
                                   uint8_t hours, uint8_t minutes, uint8_t seconds,
                                   uint8_t valid_flags, uint8_t fix_type, uint8_t flags,
                                   uint8_t sats, int32_t lon, int32_t lat) {
    memset(buf, 0, 2000);
    buf[0] = UBX_SYNC1; buf[1] = UBX_SYNC2;
    buf[2] = 0x01; buf[3] = 0x07; buf[4] = 0x5C; buf[5] = 0x00;
    buf[10] = (uint8_t)(year & 0xFF);
    buf[11] = (uint8_t)((year >> 8) & 0xFF);
    buf[12] = month; buf[13] = day; buf[14] = hours; buf[15] = minutes; buf[16] = seconds;
    buf[17] = valid_flags;
    buf[26] = fix_type;
    buf[27] = flags;
    buf[29] = sats;
    buf[30] = (uint8_t)(lon & 0xFF);
    buf[31] = (uint8_t)((lon >> 8) & 0xFF);
    buf[32] = (uint8_t)((lon >> 16) & 0xFF);
    buf[33] = (uint8_t)((lon >> 24) & 0xFF);
    buf[34] = (uint8_t)(lat & 0xFF);
    buf[35] = (uint8_t)((lat >> 8) & 0xFF);
    buf[36] = (uint8_t)((lat >> 16) & 0xFF);
    buf[37] = (uint8_t)((lat >> 24) & 0xFF);
}

static void reset_clock_and_tick(void) { g_fake_now = 1000; g_fake_tick = 0; }

static void test_poll_pvt_success(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    neo_pvt_t pvt;
    reset_clock_and_tick();

    fill_nav_pvt_response(m.canned_response, 2024, 5, 20, 10, 15, 30, 0x07, 3, 0x01, 8, 100, 200);
    m.have_canned_response = 1;

    int ok = neo_m8t_poll_pvt(&t, &pvt, fake_now_ms);
    assert(ok == 1);
    assert(pvt.year == 2024);
    assert(pvt.sats == 8);
    assert(m.cs_enable_calls == m.cs_disable_calls); /* CS always paired */

    printf("test_poll_pvt_success OK\n");
}

/* Advance the fake clock a fixed step every time the mock transfer is
 * invoked, so process_ubx_message's internal retry loop naturally
 * reaches its timeout without an infinite loop in the test. */
static void m_transfer_advancing_clock(void *ctx, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len)
{
    m_transfer(ctx, tx, tx_len, rx, rx_len);
    g_fake_now += 200;
}

static void test_poll_pvt_timeout_advances_and_fails(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    t.transfer = m_transfer_advancing_clock;
    neo_pvt_t pvt;
    reset_clock_and_tick();
    m.have_canned_response = 0;

    int ok = neo_m8t_poll_pvt(&t, &pvt, fake_now_ms);
    assert(ok == 0);
    assert(m.transfer_calls > 1); /* retried at least once before giving up */

    printf("test_poll_pvt_timeout_advances_and_fails OK\n");
}

static void test_signal_status_scales_and_caps_at_100(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    reset_clock_and_tick();

    /* 20 sats * 7 = 140 -> capped at 100 */
    fill_nav_pvt_response(m.canned_response, 2024, 1, 1, 0, 0, 0, 0x07, 3, 0x01, 20, 0, 0);
    m.have_canned_response = 1;

    int raw_sats = -99, status = -99;
    int sats_pct = neo_m8t_update_signal_status(&t, fake_now_ms, &raw_sats, &status);
    assert(sats_pct == 100);
    assert(raw_sats == 20); /* uncapped raw count, distinct from the scaled/capped tank value */
    assert(status == 1);

    printf("test_signal_status_scales_and_caps_at_100 OK\n");
}

static void test_signal_status_uncapped(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    reset_clock_and_tick();

    /* 5 sats * 7 = 35 -- under the cap */
    fill_nav_pvt_response(m.canned_response, 2024, 1, 1, 0, 0, 0, 0x07, 3, 0x01, 5, 0, 0);
    m.have_canned_response = 1;

    int sats_pct = neo_m8t_update_signal_status(&t, fake_now_ms, NULL, NULL);
    assert(sats_pct == 35);

    printf("test_signal_status_uncapped OK\n");
}

static void test_signal_status_poll_failure_returns_negative(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    t.transfer = m_transfer_advancing_clock;
    reset_clock_and_tick();
    m.have_canned_response = 0;

    int raw_sats = -99, status = -99;
    int result = neo_m8t_update_signal_status(&t, fake_now_ms, &raw_sats, &status);
    assert(result == -1);
    assert(raw_sats == -1); /* out-params also report failure, not left uninitialized */
    assert(status == -1);

    printf("test_signal_status_poll_failure_returns_negative OK\n");
}

static uint32_t g_step_tick_calls;
static uint32_t stepping_tick_count(void) {
    g_step_tick_calls++;
    /* first call (used as start_tick) returns 5; every call after
     * that returns 6, simulating the PPS edge arriving right after
     * the initial snapshot */
    return (g_step_tick_calls <= 1) ? 5u : 6u;
}

static void test_sync_time_success_with_stepping_tick(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    rtc_datetime_t out_time;
    reset_clock_and_tick();
    g_step_tick_calls = 0;

    /* time zone 0, add30 0, no extra_second complexity -- verifies the
     * plain field mapping and that rtc_apply_seconds_offset is invoked
     * with offset 0 giving back essentially the same date/time
     * (modulo wday recomputation). */
    fill_nav_pvt_response(m.canned_response, 2024, 3, 10, 14, 20, 0, 0x07, 3, 0x01, 8, 0, 0);
    m.have_canned_response = 1;

    int ok = gps_sync_time_from_fix_impl(&t, &out_time, 0, 0, fake_now_ms, stepping_tick_count);
    assert(ok == 1);
    assert(out_time.year == 2024);
    assert(out_time.mon == 3);
    assert(out_time.mday == 10);
    assert(out_time.hour == 14);
    assert(out_time.min == 20);
    /* extra_second = tick_count()-1-start_tick; start_tick captured on
     * the FIRST call (=5), and the check-loop's tick_count_fn() calls
     * return 6 from then on, so extra_second = 6-1-5 = 0 */
    assert(out_time.sec == 0);

    printf("test_sync_time_success_with_stepping_tick OK\n");
}

static uint32_t g_never_ticks_calls;
static uint32_t never_advancing_tick(void) { g_never_ticks_calls++; return 5u; }
static uint32_t g_timeout_clock;
static uint32_t advancing_clock_fn(void) { g_timeout_clock += 100; return g_timeout_clock; }

static void test_sync_time_never_ticks_times_out(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    rtc_datetime_t out_time;
    g_timeout_clock = 0;

    fill_nav_pvt_response(m.canned_response, 2024, 3, 10, 14, 20, 0, 0x07, 3, 0x01, 8, 0, 0);
    m.have_canned_response = 1;

    int ok = gps_sync_time_from_fix_impl(&t, &out_time, 0, 0, advancing_clock_fn, never_advancing_tick);
    assert(ok == 0); /* PPS never ticked past start_tick -- 2000ms timeout */

    printf("test_sync_time_never_ticks_times_out OK\n");
}

static void test_sync_time_applies_timezone_and_add30(void) {
    mock_spi_t m; memset(&m, 0, sizeof(m));
    neo_m8t_transport_t t = make_transport(&m);
    rtc_datetime_t out_time;
    reset_clock_and_tick();
    g_step_tick_calls = 0;

    /* 23:50:00 + 2 hours (tz) + 30 min (add30) should roll into the
     * next day at 02:20:00 -- exercises that rtc_apply_seconds_offset
     * is really being called with the combined offset, not just one
     * term. */
    fill_nav_pvt_response(m.canned_response, 2024, 3, 10, 23, 50, 0, 0x07, 3, 0x01, 8, 0, 0);
    m.have_canned_response = 1;

    int ok = gps_sync_time_from_fix_impl(&t, &out_time, 2, 1, fake_now_ms, stepping_tick_count);
    assert(ok == 1);
    assert(out_time.mday == 11);
    assert(out_time.hour == 2);
    assert(out_time.min == 20);

    printf("test_sync_time_applies_timezone_and_add30 OK\n");
}

int main(void) {
    test_poll_pvt_success();
    test_poll_pvt_timeout_advances_and_fails();
    test_signal_status_scales_and_caps_at_100();
    test_signal_status_uncapped();
    test_signal_status_poll_failure_returns_negative();
    test_sync_time_success_with_stepping_tick();
    test_sync_time_never_ticks_times_out();
    test_sync_time_applies_timezone_and_add30();
    printf("\nAll neo_m8t_reader tests passed.\n");
    return 0;
}
