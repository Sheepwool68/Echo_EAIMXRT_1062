#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "nrf_spi_protocol.h"

/* ------------------------------------------------------------------ */
/* Mock transport                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    int cs_asserted;
    int cs_assert_count;
    int cs_deassert_count;

    /* what the mock will hand back on the next transfer() call */
    const uint8_t *canned_rx;
    size_t canned_rx_len;

    /* records of what was actually sent, for assertions */
    uint8_t last_tx[900];
    size_t last_tx_len;

    /* fault injection */
    int force_transfer_error;
    int force_ready_timeout;
    int ready_line_level; /* what read_ready_line() reports */
} mock_hw_t;

static void mock_cs_assert(void *ctx) {
    mock_hw_t *hw = (mock_hw_t *)ctx;
    hw->cs_asserted = 1;
    hw->cs_assert_count++;
}
static void mock_cs_deassert(void *ctx) {
    mock_hw_t *hw = (mock_hw_t *)ctx;
    hw->cs_asserted = 0;
    hw->cs_deassert_count++;
}
static int mock_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len) {
    mock_hw_t *hw = (mock_hw_t *)ctx;
    if (hw->force_transfer_error) return -1;
    assert(hw->cs_asserted && "transfer() called with CS not asserted");
    memcpy(hw->last_tx, tx, len);
    hw->last_tx_len = len;
    if (hw->canned_rx != NULL) {
        size_t copy_len = (len < hw->canned_rx_len) ? len : hw->canned_rx_len;
        memcpy(rx, hw->canned_rx, copy_len);
        if (copy_len < len) memset(rx + copy_len, 0, len - copy_len);
    } else {
        memset(rx, 0, len);
    }
    return 0;
}
static int mock_wait_ready_line(void *ctx, int level, uint32_t timeout_ms) {
    mock_hw_t *hw = (mock_hw_t *)ctx;
    (void)timeout_ms;
    if (hw->force_ready_timeout) return -1;
    (void)level;
    return 0;
}
static int mock_read_ready_line(void *ctx) {
    mock_hw_t *hw = (mock_hw_t *)ctx;
    return hw->ready_line_level;
}
static void mock_delay_us(void *ctx, uint32_t us) { (void)ctx; (void)us; }
static void mock_delay_ms(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }

static void mock_reset(mock_hw_t *hw) {
    memset(hw, 0, sizeof(*hw));
}

static nrf_spi_transport_t make_transport(mock_hw_t *hw) {
    nrf_spi_transport_t t;
    t.hw_ctx = hw;
    t.cs_assert = mock_cs_assert;
    t.cs_deassert = mock_cs_deassert;
    t.transfer = mock_transfer;
    t.wait_ready_line = mock_wait_ready_line;
    t.read_ready_line = mock_read_ready_line;
    t.delay_us = mock_delay_us;
    t.delay_ms = mock_delay_ms;
    return t;
}

/* ------------------------------------------------------------------ */

static void test_poll_normal(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);
    uint8_t canned[2] = { 5, NRF_RECORD_TYPE_LIVE };
    hw.canned_rx = canned; hw.canned_rx_len = sizeof(canned);

    uint8_t count = 0xFF, type = 0xFF;
    nrf_spi_status_t st = nrf_spi_poll(&t, &count, &type);

    assert(st == NRF_SPI_OK);
    assert(count == 5);
    assert(type == NRF_RECORD_TYPE_LIVE);
    assert(hw.last_tx[0] == 0x01);
    assert(hw.cs_assert_count == 1 && hw.cs_deassert_count == 1);
    printf("test_poll_normal OK\n");
}

static void test_poll_ignore_sentinel(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);
    uint8_t canned[2] = { (uint8_t)NRF_SPI_POLL_IGNORE_SENTINEL, 0 };
    hw.canned_rx = canned; hw.canned_rx_len = sizeof(canned);

    uint8_t count = 0xFF, type = 0xFF;
    nrf_spi_status_t st = nrf_spi_poll(&t, &count, &type);

    assert(st == NRF_SPI_OK);
    assert(count == 0);
    printf("test_poll_ignore_sentinel OK\n");
}

static void test_poll_too_many(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);
    uint8_t canned[2] = { NRF_SPI_MAX_RECORDS + 1, NRF_RECORD_TYPE_LIVE };
    hw.canned_rx = canned; hw.canned_rx_len = sizeof(canned);

    uint8_t count = 0xFF, type = 0xFF;
    nrf_spi_status_t st = nrf_spi_poll(&t, &count, &type);

    assert(st == NRF_SPI_ERR_TOO_MANY);
    assert(count == 0);
    printf("test_poll_too_many OK\n");
}

static void test_poll_timeout(void) {
    mock_hw_t hw; mock_reset(&hw);
    hw.force_ready_timeout = 1;
    nrf_spi_transport_t t = make_transport(&hw);

    uint8_t count, type;
    nrf_spi_status_t st = nrf_spi_poll(&t, &count, &type);
    assert(st == NRF_SPI_ERR_TIMEOUT);
    printf("test_poll_timeout OK\n");
}

static void test_poll_transfer_error(void) {
    mock_hw_t hw; mock_reset(&hw);
    hw.force_transfer_error = 1;
    nrf_spi_transport_t t = make_transport(&hw);

    uint8_t count, type;
    nrf_spi_status_t st = nrf_spi_poll(&t, &count, &type);
    assert(st == NRF_SPI_ERR_TRANSFER);
    /* CS must still be deasserted even on error */
    assert(hw.cs_deassert_count == 1);
    printf("test_poll_transfer_error OK\n");
}

static void build_live_frame(uint8_t *buf, size_t frame_off,
                              uint32_t date_time, uint16_t ms,
                              int8_t rssi, uint8_t wake, uint8_t batt,
                              uint16_t loop_data, const char code[6])
{
    buf[frame_off + 2]  = (uint8_t)(date_time & 0xFF);
    buf[frame_off + 3]  = (uint8_t)((date_time >> 8) & 0xFF);
    buf[frame_off + 4]  = (uint8_t)((date_time >> 16) & 0xFF);
    buf[frame_off + 5]  = (uint8_t)((date_time >> 24) & 0xFF);
    buf[frame_off + 6]  = (uint8_t)(ms & 0xFF);
    buf[frame_off + 7]  = (uint8_t)((ms >> 8) & 0xFF);
    buf[frame_off + 8]  = (uint8_t)rssi;
    buf[frame_off + 9]  = wake;
    buf[frame_off + 10] = batt;
    buf[frame_off + 11] = 0xEE; /* mystery padding byte -- should be ignored by parser */
    buf[frame_off + 12] = (uint8_t)(loop_data & 0xFF);
    buf[frame_off + 13] = (uint8_t)((loop_data >> 8) & 0xFF);
    memcpy(&buf[frame_off + 14], code, 6);
}

static void test_retrieve_live_records(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);

    uint8_t canned[2 * NRF_SPI_LIVE_FRAME_LEN + 2];
    memset(canned, 0, sizeof(canned));
    build_live_frame(canned, 0, 1717000000UL, 250, -42, 3, 88, (2 << 6), "AAA111");
    build_live_frame(canned, NRF_SPI_LIVE_FRAME_LEN, 1717000010UL, 1500 /* triggers ms>999 fix */, 10, 1, 55, (5 << 6), "BBB222");

    hw.canned_rx = canned;
    hw.canned_rx_len = sizeof(canned);

    nrf_record_t out[4];
    int n = nrf_spi_retrieve_live_records(&t, 2, out, 4);

    assert(n == 2);

    assert(out[0].date_time == 1717000000UL);
    assert(out[0].ms == 250);
    assert(out[0].max_RSSI == -42);
    assert(out[0].wake_count == 3);
    assert(out[0].battery == 88);
    assert(out[0].loop_data == (2 << 6));
    assert(memcmp(out[0].xpdr_code, "AAA111", 6) == 0);

    /* record 2 exercises the ms > 999 wraparound fix */
    assert(out[1].ms == 500);                 /* 1500 - 1000 */
    assert(out[1].date_time == 1717000011UL); /* +1 second */
    assert(memcmp(out[1].xpdr_code, "BBB222", 6) == 0);

    assert(hw.last_tx[0] == 0x02);

    printf("test_retrieve_live_records OK\n");
}

static void test_retrieve_live_records_buffer_cap(void) {
    /* max_out smaller than count: should only fill max_out and return that many */
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);
    uint8_t canned[3 * NRF_SPI_LIVE_FRAME_LEN + 2];
    memset(canned, 0, sizeof(canned));
    build_live_frame(canned, 0, 1, 1, 1, 1, 1, 1, "CCC333");
    build_live_frame(canned, NRF_SPI_LIVE_FRAME_LEN, 2, 2, 2, 2, 2, 2, "DDD444");
    build_live_frame(canned, 2 * NRF_SPI_LIVE_FRAME_LEN, 3, 3, 3, 3, 3, 3, "EEE555");
    hw.canned_rx = canned; hw.canned_rx_len = sizeof(canned);

    nrf_record_t out[2];
    int n = nrf_spi_retrieve_live_records(&t, 3, out, 2);
    assert(n == 2);
    printf("test_retrieve_live_records_buffer_cap OK\n");
}

static void test_retrieve_playback_records(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);

    uint8_t canned[2 * NRF_SPI_PLAYBACK_FRAME_LEN + 17];
    memset(canned, 0, sizeof(canned));
    memcpy(&canned[2], "ZZZ999", 6);
    uint32_t boots = 42, bt_time = 12345;
    canned[8]  = (uint8_t)(boots & 0xFF);
    canned[9]  = (uint8_t)((boots >> 8) & 0xFF);
    canned[10] = (uint8_t)((boots >> 16) & 0xFF);
    canned[11] = (uint8_t)((boots >> 24) & 0xFF);
    canned[12] = (uint8_t)(bt_time & 0xFF);
    canned[13] = (uint8_t)((bt_time >> 8) & 0xFF);
    canned[14] = (uint8_t)((bt_time >> 16) & 0xFF);
    canned[15] = (uint8_t)((bt_time >> 24) & 0xFF);
    canned[16] = 7; /* fw version */

    /* record 0: loop_id raw = 2 -> loop_id = 3 */
    canned[17] = 2;
    uint32_t dt0 = 1717000200UL;
    canned[18] = (uint8_t)(dt0 & 0xFF);
    canned[19] = (uint8_t)((dt0 >> 8) & 0xFF);
    canned[20] = (uint8_t)((dt0 >> 16) & 0xFF);
    canned[21] = (uint8_t)((dt0 >> 24) & 0xFF);
    canned[22] = 100; canned[23] = 0; /* ms = 100 */

    hw.canned_rx = canned; hw.canned_rx_len = sizeof(canned);

    nrf_record_t out[4];
    char code[7];
    uint32_t out_boots = 0, out_bt = 0;
    uint8_t out_fw = 0;

    int n = nrf_spi_retrieve_playback_records(&t, 1, out, 4, code, &out_boots, &out_bt, &out_fw);

    assert(n == 1);
    assert(strncmp(code, "ZZZ999", 6) == 0);
    assert(out_boots == 42);
    assert(out_bt == 12345);
    assert(out_fw == 7);
    assert(out[0].date_time == dt0);
    assert(out[0].ms == 100);
    assert(out[0].log_id == 0);
    assert(out[0].max_RSSI == 0);
    /* loop_id = 3 -> loop_data = ((3-1)<<6)&0x3C0 = (2<<6)&0x3C0 = 0x80 */
    assert(out[0].loop_data == 0x80);
    assert(memcmp(out[0].xpdr_code, "ZZZ999", 6) == 0);

    printf("test_retrieve_playback_records OK\n");
}

static void test_program_chip_code_checksum(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);

    char code[6] = { 'A', 'B', 'C', '1', '2', '3' };
    uint8_t crc_out = 0;
    nrf_spi_status_t st = nrf_spi_program_chip_code(&t, code, &crc_out);
    assert(st == NRF_SPI_OK);

    uint8_t expected = 0xAA;
    for (int i = 0; i < 6; i++) expected ^= (uint8_t)code[i];
    assert(crc_out == expected);

    /* Second phase transfer: cmdb[0]=0x06, [1..6]=code, [7]=crc */
    assert(hw.last_tx[0] == 0x06);
    assert(memcmp(&hw.last_tx[1], code, 6) == 0);
    assert(hw.last_tx[7] == expected);

    printf("test_program_chip_code_checksum OK\n");
}

static void test_send_datetime_byte_order(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);

    uint32_t ts = 0x01020304UL;
    nrf_spi_status_t st = nrf_spi_send_datetime(&t, ts);
    assert(st == NRF_SPI_OK);

    /* Second phase: cmdb[0]=0x09, then big-endian time bytes */
    assert(hw.last_tx[0] == 0x09);
    assert(hw.last_tx[1] == 0x01);
    assert(hw.last_tx[2] == 0x02);
    assert(hw.last_tx[3] == 0x03);
    assert(hw.last_tx[4] == 0x04);

    printf("test_send_datetime_byte_order OK\n");
}

static void test_get_battery_percent(void) {
    mock_hw_t hw; mock_reset(&hw);
    nrf_spi_transport_t t = make_transport(&hw);
    uint8_t canned[2] = { 0x00, 77 };
    hw.canned_rx = canned; hw.canned_rx_len = sizeof(canned);

    uint8_t pct = 0;
    nrf_spi_status_t st = nrf_spi_get_battery_percent(&t, &pct);
    assert(st == NRF_SPI_OK);
    assert(pct == 77);
    assert(hw.last_tx[0] == 0x0D);
    printf("test_get_battery_percent OK\n");
}

static void test_simple_commands_cs_discipline(void) {
    /* Every simple command must assert CS exactly once and deassert
     * exactly once, even variety across the whole command set. */
    mock_hw_t hw;
    nrf_spi_transport_t t;

    #define CHECK_CS_DISCIPLINE(callexpr) do { \
        mock_reset(&hw); t = make_transport(&hw); \
        (void)(callexpr); \
        assert(hw.cs_assert_count == hw.cs_deassert_count); \
        assert(hw.cs_assert_count >= 1); \
    } while (0)

    CHECK_CS_DISCIPLINE(nrf_spi_set_reader_power(&t, 80));
    CHECK_CS_DISCIPLINE(nrf_spi_set_bt_advertising(&t, 2));
    CHECK_CS_DISCIPLINE(nrf_spi_set_playback_mode(&t, 1));
    CHECK_CS_DISCIPLINE(nrf_spi_set_channel(&t, 4));
    CHECK_CS_DISCIPLINE(nrf_spi_set_sleep_mode(&t, 1));
    CHECK_CS_DISCIPLINE(nrf_spi_transmitter_off(&t));
    CHECK_CS_DISCIPLINE(nrf_spi_enter_dfu(&t));

    #undef CHECK_CS_DISCIPLINE
    printf("test_simple_commands_cs_discipline OK\n");
}

int main(void) {
    test_poll_normal();
    test_poll_ignore_sentinel();
    test_poll_too_many();
    test_poll_timeout();
    test_poll_transfer_error();
    test_retrieve_live_records();
    test_retrieve_live_records_buffer_cap();
    test_retrieve_playback_records();
    test_program_chip_code_checksum();
    test_send_datetime_byte_order();
    test_get_battery_percent();
    test_simple_commands_cs_discipline();
    printf("\nAll nRF SPI protocol tests passed.\n");
    return 0;
}
