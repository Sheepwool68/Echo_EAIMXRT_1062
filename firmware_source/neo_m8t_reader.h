/*
 * neo_m8t_reader.h
 *
 * Orchestration layer for NEOM8T.LIB -- was process_UBX(), set_UBX(),
 * PVT_report(), GetGPS_Signal(), SetTime_uBlox(). Built on
 * neo_m8t_protocol.h's pure checksum/scan/parse logic.
 *
 * Matches gps_stub.h's existing contract (see that file's comments)
 * rather than the original's exactly -- specifically, gps_sync_time_from_fix()
 * here computes and returns the new time via an out-param instead of
 * writing the RTC itself (the caller in app_loop.c already does
 * ds3231_rt1062_write() + its own bookkeeping after a successful call,
 * matching the established pattern from every other stub replaced so
 * far in this port).
 */

#ifndef NEO_M8T_READER_H
#define NEO_M8T_READER_H

#include "neo_m8t_protocol.h"
#include "rtc_time.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;
    void (*cs_enable)(void *ctx);
    void (*cs_disable)(void *ctx);
    void (*delay_ms)(void *ctx, uint32_t ms);
    void (*transfer)(void *ctx, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len);
} neo_m8t_transport_t;

int process_ubx_message(const neo_m8t_transport_t *t, uint8_t *command, size_t length,
                         int message_type, neo_pvt_t *out_pvt, uint32_t (*now_ms_fn)(void));

void neo_m8t_configure_timepulse(const neo_m8t_transport_t *t, uint32_t (*now_ms_fn)(void));

int neo_m8t_poll_pvt(const neo_m8t_transport_t *t, neo_pvt_t *out_pvt, uint32_t (*now_ms_fn)(void));

int neo_m8t_update_signal_status(const neo_m8t_transport_t *t, uint32_t (*now_ms_fn)(void));

int gps_sync_time_from_fix_impl(const neo_m8t_transport_t *t, rtc_datetime_t *out_time,
                                 int time_zone_hours, int add30,
                                 uint32_t (*now_ms_fn)(void), uint32_t (*tick_count_fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* NEO_M8T_READER_H */
