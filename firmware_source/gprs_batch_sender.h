/*
 * gprs_batch_sender.h
 *
 * Ported from 4G_Modem.lib's Remote_SendNextBatch() +
 * Remote_SendNextBatchToSocket() combined.
 *
 * Bridges storage (nand_log_littlefs.h) to whichever transport is
 * active (gprs_modem.h for RemoteType==1, tcp_session.h's socket write
 * for RemoteType==2) via two small function-pointer seams, so this
 * module doesn't need to know about either concretely -- matches the
 * byte-sink pattern used elsewhere in this port.
 *
 * FLAGGED IMPROVEMENT: the original never checks serFwrite/
 * sock_fastwrite's return value -- if a write silently failed,
 * Settings.GPRS_CurrentRec would still advance as if it succeeded,
 * permanently skipping that record from ever being resent. This port
 * checks the sink's return value and stops the batch (without
 * advancing io_current_rec for the failed record) on a write failure.
 * This is a deliberate behavior change, flagged rather than silently
 * added.
 */

#ifndef GPRS_BATCH_SENDER_H
#define GPRS_BATCH_SENDER_H

#include <stdint.h>
#include <stddef.h>
#include "nrf_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GPRS_RECORDS_PER_BATCH 50

/* Was `fat_Seek(&NANDLogFile, record_index * sizeof(logEntry), SEEK_SET)`.
 * Returns 0 on success. */
typedef int (*gprs_record_seek_fn)(void *ctx, uint64_t record_index);

/* Was `fat_Read(&NANDLogFile, &logEntry, sizeof(logEntry))`. Returns 1
 * if a record was read, 0 on EOF/no more records, negative on error. */
typedef int (*gprs_record_read_fn)(void *ctx, nrf_record_t *out);

/* Was serFwrite()/sock_fastwrite(). Returns bytes written, or <= 0 on failure. */
typedef int (*gprs_byte_sink_fn)(void *ctx, const uint8_t *buf, size_t len);

typedef struct {
    void *source_ctx;
    gprs_record_seek_fn seek;
    gprs_record_read_fn read;

    void *sink_ctx;
    gprs_byte_sink_fn sink;

    uint8_t channel;
    uint8_t mac_address[6];
} gprs_batch_sender_config_t;

/*
 * Sends up to GPRS_RECORDS_PER_BATCH records starting at *io_current_rec,
 * advancing it by however many were actually sent successfully.
 * Returns the number of records sent (was 'k' -- if > 0, the caller
 * should update its modem-state timers/status, e.g. via gprs_modem_t,
 * matching the original's GPRS_BatchSendTime/GPRS_STATE/GPRS_STATUS
 * updates -- left to the caller since that's gprs_modem.h's domain,
 * not this module's), or negative on a seek/read/write error.
 */
int gprs_send_next_batch(const gprs_batch_sender_config_t *cfg, uint32_t *io_current_rec);

#ifdef __cplusplus
}
#endif

#endif /* GPRS_BATCH_SENDER_H */
