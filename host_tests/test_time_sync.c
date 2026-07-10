#include <stdio.h>
#include <assert.h>
#include "time_sync.h"

static void test_display_refresh_seconds_only(void) {
    rtc_datetime_t dt = { 2024, 7, 3, 14, 5, 37, 3 }; /* sec != 0 */
    time_display_actions_t a = time_sync_display_refresh_actions(&dt);
    assert(a.refresh_seconds == 1);
    assert(a.refresh_minutes == 0);
    assert(a.refresh_hours == 0);
    assert(a.refresh_date == 0);
    printf("test_display_refresh_seconds_only OK\n");
}

static void test_display_refresh_minute_rollover(void) {
    rtc_datetime_t dt = { 2024, 7, 3, 14, 6, 0, 3 }; /* sec==0, min!=0 */
    time_display_actions_t a = time_sync_display_refresh_actions(&dt);
    assert(a.refresh_seconds == 1);
    assert(a.refresh_minutes == 1);
    assert(a.refresh_hours == 0);
    assert(a.refresh_date == 0);
    printf("test_display_refresh_minute_rollover OK\n");
}

static void test_display_refresh_hour_rollover(void) {
    rtc_datetime_t dt = { 2024, 7, 3, 15, 0, 0, 3 }; /* sec==0, min==0, hour!=0 */
    time_display_actions_t a = time_sync_display_refresh_actions(&dt);
    assert(a.refresh_minutes == 1);
    assert(a.refresh_hours == 1);
    assert(a.refresh_date == 0);
    printf("test_display_refresh_hour_rollover OK\n");
}

static void test_display_refresh_midnight_rollover(void) {
    rtc_datetime_t dt = { 2024, 7, 4, 0, 0, 0, 4 }; /* sec==0, min==0, hour==0 */
    time_display_actions_t a = time_sync_display_refresh_actions(&dt);
    assert(a.refresh_seconds == 1);
    assert(a.refresh_minutes == 1);
    assert(a.refresh_hours == 1);
    assert(a.refresh_date == 1);
    printf("test_display_refresh_midnight_rollover OK\n");
}

static void test_pps_edge_detection(void) {
    assert(pps_edge_detected(1, 0) == 1);
    assert(pps_edge_detected(0, 1) == 1);
    assert(pps_edge_detected(1, 1) == 0);
    assert(pps_edge_detected(0, 0) == 0);
    printf("test_pps_edge_detection OK\n");
}

static void test_pps_gps_sync_gating(void) {
    /* Only sync if not already time-set AND auto-sync enabled */
    assert(pps_should_trigger_gps_sync(0, 1) == 1);
    assert(pps_should_trigger_gps_sync(1, 1) == 0); /* already synced */
    assert(pps_should_trigger_gps_sync(0, 0) == 0); /* auto-sync disabled */
    assert(pps_should_trigger_gps_sync(1, 0) == 0);
    printf("test_pps_gps_sync_gating OK\n");
}

static void test_nrf_push_gating_immediate(void) {
    int pending = 1;
    int should_push = time_sync_maybe_push_nrf_time(&pending, 0 /* spi free */);
    assert(should_push == 1);
    assert(pending == 0); /* cleared after push */
    printf("test_nrf_push_gating_immediate OK\n");
}

static void test_nrf_push_gating_retries_while_busy(void) {
    int pending = 1;

    /* SPI busy: should not push yet, flag stays pending */
    int should_push = time_sync_maybe_push_nrf_time(&pending, 1 /* busy */);
    assert(should_push == 0);
    assert(pending == 1);

    should_push = time_sync_maybe_push_nrf_time(&pending, 1 /* still busy */);
    assert(should_push == 0);
    assert(pending == 1);

    /* SPI now free: pushes and clears */
    should_push = time_sync_maybe_push_nrf_time(&pending, 0);
    assert(should_push == 1);
    assert(pending == 0);

    /* subsequent calls with nothing pending: no-op */
    should_push = time_sync_maybe_push_nrf_time(&pending, 0);
    assert(should_push == 0);

    printf("test_nrf_push_gating_retries_while_busy OK\n");
}

int main(void) {
    test_display_refresh_seconds_only();
    test_display_refresh_minute_rollover();
    test_display_refresh_hour_rollover();
    test_display_refresh_midnight_rollover();
    test_pps_edge_detection();
    test_pps_gps_sync_gating();
    test_nrf_push_gating_immediate();
    test_nrf_push_gating_retries_while_busy();
    printf("\nAll time_sync tests passed.\n");
    return 0;
}
