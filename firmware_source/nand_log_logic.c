#include "nand_log_logic.h"

int nand_log_percent_full(uint64_t file_size_bytes, uint64_t max_size_bytes)
{
    uint64_t pct;

    if (max_size_bytes == 0) {
        return 0; /* avoid division by zero; treat an unconfigured limit as "not full" */
    }
    if (file_size_bytes >= max_size_bytes) {
        return 100;
    }

    pct = (file_size_bytes * 100u) / max_size_bytes;
    if (pct > 100u) {
        pct = 100u; /* defensive clamp, shouldn't trigger given the check above */
    }
    return (int)pct;
}

int nand_log_should_auto_reset(int percent_full)
{
    return percent_full >= NAND_LOG_AUTO_RESET_THRESHOLD_PERCENT;
}

uint64_t nand_log_record_count_from_size(uint64_t file_size_bytes)
{
    return file_size_bytes / sizeof(nrf_record_t);
}

uint64_t nand_log_offset_for_record(uint64_t record_index)
{
    return record_index * sizeof(nrf_record_t);
}
