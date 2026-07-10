/*
 * nrf_spi_protocol.c
 *
 * See nrf_spi_protocol.h for the porting rationale. This file has no
 * dependency on any NXP SDK header -- it compiles and unit-tests on
 * the host against a mock nrf_spi_transport_t, and links unchanged
 * into the RT1062 firmware image once wired to the real transport
 * (nrf_spi_transport_rt1062.c).
 *
 * FLAGGED DEVIATIONS FROM THE ORIGINAL (all deliberate -- see comments
 * at each site):
 *   1. Every wait-for-ready-line now has a timeout (transport-level).
 *   2. The CS-enable setup delay is applied consistently before every
 *      SPI phase. The original omitted it before the second phase of
 *      the 0x05 (program chip code) command specifically -- likely an
 *      oversight rather than an intentional asymmetry -- so this port
 *      adds it there too for consistency.
 *   3. Application-layer side effects (socket writes, NAND log writes,
 *      display updates, beeper, SaveSettings()) are NOT performed here.
 *      They belong in the caller. See each function's header comment
 *      for what the caller is still responsible for.
 */

#include "nrf_spi_protocol.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Little-endian byte readers (see header for why we parse explicitly  */
/* instead of memcpy'ing onto a struct).                                */
/* ------------------------------------------------------------------ */

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

/* ------------------------------------------------------------------ */
/* Common helper: assert CS, settle, do a 2-byte command/reply, drop CS */
/* ------------------------------------------------------------------ */

static nrf_spi_status_t simple_command(const nrf_spi_transport_t *t,
                                        uint8_t cmd, uint8_t arg,
                                        uint8_t reply_out[2])
{
    uint8_t tx[2] = { cmd, arg };
    uint8_t rx[2] = { 0, 0 };
    int rc;

    t->cs_assert(t->hw_ctx);
    t->delay_us(t->hw_ctx, NRF_SPI_CS_SETUP_DELAY_US);
    rc = t->transfer(t->hw_ctx, tx, rx, 2);
    t->cs_deassert(t->hw_ctx);

    if (rc != 0) {
        return NRF_SPI_ERR_TRANSFER;
    }
    if (reply_out != NULL) {
        reply_out[0] = rx[0];
        reply_out[1] = rx[1];
    }
    return NRF_SPI_OK;
}

/* ------------------------------------------------------------------ */
/* 0x01: poll                                                           */
/* ------------------------------------------------------------------ */

nrf_spi_status_t nrf_spi_poll(const nrf_spi_transport_t *t,
                               uint8_t *out_count,
                               uint8_t *out_record_type)
{
    uint8_t reply[2];
    nrf_spi_status_t st;

    st = simple_command(t, 0x01, 0x00, reply);
    if (st != NRF_SPI_OK) {
        return st;
    }

    /* Original waits for the ready line to go LOW after sending the
     * poll command, before trusting the reply bytes. */
    if (t->wait_ready_line(t->hw_ctx, 0, NRF_SPI_READY_TIMEOUT_MS) != 0) {
        return NRF_SPI_ERR_TIMEOUT;
    }

    if (reply[0] == NRF_SPI_POLL_IGNORE_SENTINEL) {
        /* Original: silently ignored, spi_record_state reset to 1.
         * Surfaced here as count == 0 so the caller can log/count it
         * if useful, rather than it being invisible. */
        *out_count = 0;
        *out_record_type = 0;
        return NRF_SPI_OK;
    }

    if (reply[0] > NRF_SPI_MAX_RECORDS) {
        *out_count = 0;
        *out_record_type = 0;
        return NRF_SPI_ERR_TOO_MANY;
    }

    *out_count = reply[0];
    *out_record_type = reply[1];
    return NRF_SPI_OK;
}

/* ------------------------------------------------------------------ */
/* 0x02, record_type == 1: live records                                */
/* ------------------------------------------------------------------ */

int nrf_spi_retrieve_live_records(const nrf_spi_transport_t *t,
                                   uint8_t count,
                                   nrf_record_t *out,
                                   size_t max_out)
{
    /* Worst case buffer: MAX_RECORDS frames of 20 bytes + 2 header bytes */
    static const size_t kMaxLen = (size_t)NRF_SPI_MAX_RECORDS * NRF_SPI_LIVE_FRAME_LEN + 2;
    uint8_t tx[(size_t)NRF_SPI_MAX_RECORDS * NRF_SPI_LIVE_FRAME_LEN + 2];
    uint8_t rx[(size_t)NRF_SPI_MAX_RECORDS * NRF_SPI_LIVE_FRAME_LEN + 2];
    size_t total_len;
    int rc;
    size_t n, out_n;

    if (count > NRF_SPI_MAX_RECORDS) {
        return NRF_SPI_ERR_TOO_MANY;
    }
    total_len = (size_t)count * NRF_SPI_LIVE_FRAME_LEN + 2;
    if (total_len > kMaxLen) {
        return NRF_SPI_ERR_BUF_TOO_SMALL; /* should not happen given the count check above */
    }

    memset(tx, 0, total_len);
    tx[0] = 0x02;
    tx[1] = 0x00;

    t->cs_assert(t->hw_ctx);
    t->delay_us(t->hw_ctx, NRF_SPI_CS_SETUP_DELAY_US);
    rc = t->transfer(t->hw_ctx, tx, rx, total_len);
    t->cs_deassert(t->hw_ctx);
    if (rc != 0) {
        return NRF_SPI_ERR_TRANSFER;
    }

    if (t->wait_ready_line(t->hw_ctx, 0, NRF_SPI_READY_TIMEOUT_MS) != 0) {
        return NRF_SPI_ERR_TIMEOUT;
    }

    out_n = (count < max_out) ? count : max_out;

    for (n = 0; n < out_n; n++) {
        size_t frame = (size_t)n * NRF_SPI_LIVE_FRAME_LEN;
        nrf_record_t *rec = &out[n];
        uint32_t date_time = rd_le32(&rx[frame + 2]);
        uint16_t ms = rd_le16(&rx[frame + 6]);

        rec->date_time = date_time;
        rec->ms = ms;
        rec->max_RSSI = (int8_t)rx[frame + 8];
        rec->wake_count = rx[frame + 9];
        rec->battery = rx[frame + 10];
        /* rx[frame + 11] is the undocumented padding byte from the
         * original firmware's wire format -- intentionally skipped.
         * See file header notes. Worth confirming with the nRF52833
         * firmware source whether this byte carries any real data. */
        rec->loop_data = rd_le16(&rx[frame + 12]);
        memcpy(rec->xpdr_code, &rx[frame + 14], 6);
        rec->has_been_sent = 0;
        rec->log_id = 0; /* caller assigns (was iLastLogID++ in the original main loop) */

        /* Original workaround for an nRF-firmware bug where ms can
         * exceed 999. Kept here since it's about correctly interpreting
         * a malformed wire value, not application logic. TODO: confirm
         * whether this is still needed on the current nRF52833 firmware
         * version before relying on it long-term. */
        if (rec->ms > 999) {
            rec->ms = (uint16_t)(rec->ms - 1000);
            rec->date_time += 1;
        }
    }

    return (int)out_n;
}

/* ------------------------------------------------------------------ */
/* 0x02, record_type == 2: playback records                            */
/* ------------------------------------------------------------------ */

int nrf_spi_retrieve_playback_records(const nrf_spi_transport_t *t,
                                       uint8_t count,
                                       nrf_record_t *out,
                                       size_t max_out,
                                       char out_code[7],
                                       uint32_t *out_boots,
                                       uint32_t *out_bt_time,
                                       uint8_t *out_fw_version)
{
    static const size_t kMaxLen = (size_t)NRF_SPI_MAX_RECORDS * NRF_SPI_PLAYBACK_FRAME_LEN + 17;
    uint8_t tx[(size_t)NRF_SPI_MAX_RECORDS * NRF_SPI_PLAYBACK_FRAME_LEN + 17];
    uint8_t rx[(size_t)NRF_SPI_MAX_RECORDS * NRF_SPI_PLAYBACK_FRAME_LEN + 17];
    size_t total_len;
    int rc;
    size_t n, out_n;
    char header_code[6];

    if (count > NRF_SPI_MAX_RECORDS) {
        return NRF_SPI_ERR_TOO_MANY;
    }
    total_len = (size_t)count * NRF_SPI_PLAYBACK_FRAME_LEN + 17; /* 8 + 9, matches original's "+8+9" */
    if (total_len > kMaxLen) {
        return NRF_SPI_ERR_BUF_TOO_SMALL;
    }

    memset(tx, 0, total_len);
    tx[0] = 0x02;
    tx[1] = 0x00;

    t->cs_assert(t->hw_ctx);
    t->delay_us(t->hw_ctx, NRF_SPI_CS_SETUP_DELAY_US);
    rc = t->transfer(t->hw_ctx, tx, rx, total_len);
    t->cs_deassert(t->hw_ctx);
    if (rc != 0) {
        return NRF_SPI_ERR_TRANSFER;
    }

    /* Note: the original does not wait for the ready line to drop in
     * this branch (record_type == 2) the way it does for live records
     * -- it reads spi_recv_buff immediately after CS1_DISABLE. Ported
     * faithfully; flagging in case that was actually a bug in the
     * original rather than intentional. */

    memcpy(header_code, &rx[2], 6);
    if (out_code != NULL) {
        memcpy(out_code, header_code, 6);
        out_code[6] = '\0';
    }
    if (out_boots != NULL)      *out_boots = rd_le32(&rx[8]);
    if (out_bt_time != NULL)    *out_bt_time = rd_le32(&rx[12]);
    if (out_fw_version != NULL) *out_fw_version = rx[16];

    out_n = (count < max_out) ? count : max_out;

    for (n = 0; n < out_n; n++) {
        size_t frame = (size_t)n * NRF_SPI_PLAYBACK_FRAME_LEN;
        nrf_record_t *rec = &out[n];
        uint8_t loop_id = (uint8_t)(rx[frame + 17] + 1);

        memcpy(rec->xpdr_code, header_code, 6);
        rec->loop_data = (uint16_t)(((uint16_t)(loop_id - 1) << 6) & 0x3C0);
        rec->date_time = rd_le32(&rx[frame + 18]);
        rec->ms = rd_le16(&rx[frame + 22]);
        rec->log_id = 0;      /* preserved from original: playback records always LogID 0 */
        rec->max_RSSI = 0;
        rec->wake_count = 0;
        rec->battery = 0;
        rec->has_been_sent = 0;
    }

    return (int)out_n;
}

/* ------------------------------------------------------------------ */
/* Simple setter commands                                              */
/* ------------------------------------------------------------------ */

nrf_spi_status_t nrf_spi_set_reader_power(const nrf_spi_transport_t *t, uint8_t power_percent)
{
    /* Caller is responsible for persisting Settings.ReaderPower
     * afterwards (was SaveSettings() in the original). */
    return simple_command(t, 0x03, power_percent, NULL);
}

nrf_spi_status_t nrf_spi_set_bt_advertising(const nrf_spi_transport_t *t, uint8_t bt_adv_index)
{
    return simple_command(t, 0x04, bt_adv_index, NULL);
}

nrf_spi_status_t nrf_spi_program_chip_code(const nrf_spi_transport_t *t,
                                            const char code[6],
                                            uint8_t *out_crc)
{
    nrf_spi_status_t st;
    uint8_t cmdb[8];
    uint8_t rx[8];
    uint8_t crc;
    int i;
    int rc;

    /* Phase 1: announce the operation */
    st = simple_command(t, 0x05, 0x00, NULL);
    if (st != NRF_SPI_OK) {
        return st;
    }
    t->delay_ms(t->hw_ctx, 1);

    /* Same checksum as the original: seed 0xAA XORed with each code byte */
    crc = 0xAA;
    for (i = 0; i < 6; i++) {
        crc ^= (uint8_t)code[i];
    }

    cmdb[0] = 0x06;
    memcpy(&cmdb[1], code, 6);
    cmdb[7] = crc;

    /* Phase 2: send the code + checksum.
     * DEVIATION: the original omitted the CS setup delay here
     * specifically (see file header) -- added for consistency. */
    t->cs_assert(t->hw_ctx);
    t->delay_us(t->hw_ctx, NRF_SPI_CS_SETUP_DELAY_US);
    rc = t->transfer(t->hw_ctx, cmdb, rx, sizeof(cmdb));
    t->cs_deassert(t->hw_ctx);
    if (rc != 0) {
        return NRF_SPI_ERR_TRANSFER;
    }

    if (out_crc != NULL) {
        *out_crc = crc;
    }
    return NRF_SPI_OK;
}

nrf_spi_status_t nrf_spi_set_playback_mode(const nrf_spi_transport_t *t, uint8_t playback)
{
    return simple_command(t, 0x07, playback, NULL);
}

nrf_spi_status_t nrf_spi_send_datetime(const nrf_spi_transport_t *t, uint32_t unix_time)
{
    nrf_spi_status_t st;
    uint8_t cmdb[5];
    uint8_t rx[5];
    int rc;

    /* Phase 1: announce the operation */
    st = simple_command(t, 0x08, 0x00, NULL);
    if (st != NRF_SPI_OK) {
        return st;
    }
    t->delay_ms(t->hw_ctx, 1);

    /* Phase 2: send time, big-endian, matching the original's
     * (nrftime >> 24)/(>> 16)/(>> 8)/(& 0xFF) byte order */
    cmdb[0] = 0x09;
    cmdb[1] = (uint8_t)((unix_time >> 24) & 0xFF);
    cmdb[2] = (uint8_t)((unix_time >> 16) & 0xFF);
    cmdb[3] = (uint8_t)((unix_time >> 8) & 0xFF);
    cmdb[4] = (uint8_t)(unix_time & 0xFF);

    t->cs_assert(t->hw_ctx);
    t->delay_us(t->hw_ctx, NRF_SPI_CS_SETUP_DELAY_US);
    rc = t->transfer(t->hw_ctx, cmdb, rx, sizeof(cmdb));
    t->cs_deassert(t->hw_ctx);
    if (rc != 0) {
        return NRF_SPI_ERR_TRANSFER;
    }

    t->delay_ms(t->hw_ctx, 1);
    return NRF_SPI_OK;
}

nrf_spi_status_t nrf_spi_set_channel(const nrf_spi_transport_t *t, uint8_t channel)
{
    /* Caller persists Settings.Channel (was SaveSettings() in the original). */
    return simple_command(t, 0x0A, channel, NULL);
}

nrf_spi_status_t nrf_spi_set_sleep_mode(const nrf_spi_transport_t *t, uint8_t sleep)
{
    return simple_command(t, 0x0B, sleep, NULL);
}

nrf_spi_status_t nrf_spi_transmitter_off(const nrf_spi_transport_t *t)
{
    return simple_command(t, 0x0C, 0x00, NULL);
}

nrf_spi_status_t nrf_spi_get_battery_percent(const nrf_spi_transport_t *t, uint8_t *out_percent)
{
    uint8_t reply[2];
    nrf_spi_status_t st = simple_command(t, 0x0D, 0x00, reply);
    if (st != NRF_SPI_OK) {
        return st;
    }
    *out_percent = reply[1];
    return NRF_SPI_OK;
}

nrf_spi_status_t nrf_spi_get_fw_version(const nrf_spi_transport_t *t, uint8_t *out_version)
{
    uint8_t reply[2];
    nrf_spi_status_t st = simple_command(t, 0x0E, 0x00, reply);
    if (st != NRF_SPI_OK) {
        return st;
    }
    *out_version = reply[1];
    return NRF_SPI_OK;
}

nrf_spi_status_t nrf_spi_enter_dfu(const nrf_spi_transport_t *t)
{
    return simple_command(t, 0xFA, 0x00, NULL);
}
