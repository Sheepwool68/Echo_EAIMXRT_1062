#include "gprs_batch_sender.h"
#include "gprs_records.h"

int gprs_send_next_batch(const gprs_batch_sender_config_t *cfg, uint32_t *io_current_rec)
{
    int k = 0;
    int j;

    if (cfg->seek(cfg->source_ctx, *io_current_rec) != 0) {
        return -1;
    }

    for (j = 0; j < GPRS_RECORDS_PER_BATCH; j++) {
        nrf_record_t entry;
        int rc = cfg->read(cfg->source_ctx, &entry);

        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            break;
        }

        {
            uint8_t wire[64];
            gprs_record_kind_t kind;
            size_t n = gprs_build_record(&entry, cfg->channel, cfg->mac_address,
                                          *io_current_rec, wire, sizeof(wire), &kind);
            if (n == 0) {
                return -1;
            }
            if (cfg->sink(cfg->sink_ctx, wire, n) <= 0) {
                return -1;
            }
        }

        k++;
        (*io_current_rec)++;
    }

    return k;
}
