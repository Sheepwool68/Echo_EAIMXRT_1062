#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "uhf_reader.h"

#define MAX_WRITES 32
typedef struct {
    uint8_t data[64];
    size_t len;
} recorded_write_t;

#define MAX_READS 16
typedef struct {
    int err;                /* if nonzero, this read call returns -1 */
    const uint8_t *data;    /* reply bytes (NULL + err==0 -> returns 0) */
    size_t len;
} scripted_read_t;

typedef struct {
    int open_calls;
    uint32_t last_baud;
    int flush_rx_calls;
    int flush_tx_calls;

    recorded_write_t writes[MAX_WRITES];
    int write_count;

    const uint8_t *canned_reply;
    size_t canned_reply_len;

    /* Optional per-read-call script. When script_len > 0, read call k
       (0-based) is answered from scripts[k] instead of canned_reply --
       lets a test make one specific read fail (err) while others
       succeed, to exercise the "either check confirms" error path. */
    scripted_read_t scripts[MAX_READS];
    int script_len;
    int read_call_count;
} mock_uart_t;

static int m_open(void *ctx, uint32_t baud) {
    mock_uart_t *m = (mock_uart_t *)ctx;
    m->open_calls++;
    m->last_baud = baud;
    return 0;
}
static int m_write(void *ctx, const uint8_t *buf, size_t len) {
    mock_uart_t *m = (mock_uart_t *)ctx;
    if (m->write_count < MAX_WRITES) {
        memcpy(m->writes[m->write_count].data, buf, len);
        m->writes[m->write_count].len = len;
        m->write_count++;
    }
    return (int)len;
}
static int m_read(void *ctx, uint8_t *buf, size_t max_len, uint32_t timeout_ms) {
    mock_uart_t *m = (mock_uart_t *)ctx;
    (void)timeout_ms;
    int idx = m->read_call_count++;
    if (m->script_len > 0) {
        if (idx >= m->script_len) return 0;
        if (m->scripts[idx].err) return -1;
        if (m->scripts[idx].data == NULL) return 0;
        size_t sn = (m->scripts[idx].len < max_len) ? m->scripts[idx].len : max_len;
        memcpy(buf, m->scripts[idx].data, sn);
        return (int)sn;
    }
    if (m->canned_reply == NULL) return 0;
    size_t n = (m->canned_reply_len < max_len) ? m->canned_reply_len : max_len;
    memcpy(buf, m->canned_reply, n);
    return (int)n;
}
static void m_flush_rx(void *ctx) { ((mock_uart_t *)ctx)->flush_rx_calls++; }
static void m_flush_tx(void *ctx) { ((mock_uart_t *)ctx)->flush_tx_calls++; }
static void m_delay(void *ctx, uint32_t ms) { (void)ctx; (void)ms; }
static void m_close(void *ctx) { (void)ctx; }

static void mock_reset(mock_uart_t *m) {
    memset(m, 0, sizeof(*m));
}

static uhf_transport_t make_transport(mock_uart_t *m) {
    uhf_transport_t t;
    t.ctx = m;
    t.open = m_open;
    t.close = m_close;
    t.write = m_write;
    t.read = m_read;
    t.flush_rx = m_flush_rx;
    t.flush_tx = m_flush_tx;
    t.delay_ms = m_delay;
    return t;
}

static void test_open_sends_version_query(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;

    int rc = uhf_reader_open(&r, &t);

    assert(rc == 0);
    assert(m.open_calls == 1);
    assert(m.last_baud == 115200);
    assert(m.flush_rx_calls == 1 && m.flush_tx_calls == 1);
    assert(m.write_count == 1);
    assert(m.writes[0].len == 5);
    assert(m.writes[0].data[0] == 0xff && m.writes[0].data[2] == 0x03);

    printf("test_open_sends_version_query OK\n");
}

static void test_start_aborts_with_no_antennas(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;
    r.ants = 0;

    int rc = uhf_reader_start(&r, 0);
    assert(rc == 0);
    assert(m.write_count == 0);

    printf("test_start_aborts_with_no_antennas OK\n");
}

static void test_start_sends_command_when_antennas_present(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;
    r.ants = 0x0F;

    int rc = uhf_reader_start(&r, 0);
    assert(rc == 1);
    assert(m.write_count == 1);
    assert(m.writes[0].len == 24);
    assert(m.writes[0].data[0] == 0xFF && m.writes[0].data[1] == 0x13);

    printf("test_start_sends_command_when_antennas_present OK\n");
}

/* Was test_stop_sends_stop_then_temperature_query -- the trailing
 * temperature query inside uhf_reader_stop() was DISABLED 2026-07-17
 * (see uhf_reader.c's own comment at that call site): the reader
 * doesn't reliably answer it immediately after stop_reading, only when
 * genuinely idle, so it was failing there in practice and isn't
 * currently used by any caller. Updated to assert the single remaining
 * command instead of two. */
static void test_stop_sends_stop_command(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    int rc = uhf_reader_stop(&r);
    assert(rc == 0);
    assert(m.write_count == 1);
    assert(m.writes[0].len == 19);
    assert(m.writes[0].data[1] == 0x0E);

    printf("test_stop_sends_stop_command OK\n");
}

static void test_get_temperature_parses_reply(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    static const uint8_t reply[8] = {0xFF,0x00,0x72,0x00,0x00,37,0x00,0x00};
    m.canned_reply = reply;
    m.canned_reply_len = sizeof(reply);

    int temp = -1;
    int rc = uhf_reader_get_temperature(&r, &temp);
    assert(rc == 1);
    assert(temp == 37);

    printf("test_get_temperature_parses_reply OK\n");
}

static void test_set_antennae_updates_ants_from_dc_check_reply(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    static const uint8_t reply[14] = {0xFF,0x09,0x61,0x00,0x00,0x05,0x01,0x01,0x02,0x01,0x03,0x01,0x04,0x01};
    m.canned_reply = reply;
    m.canned_reply_len = sizeof(reply);

    int rc = uhf_reader_set_antennae(&r, UHF_REGION_FCC);
    assert(rc == 0);
    assert(r.ants == 0x0F);

    assert(m.write_count == 3);
    assert(m.writes[0].data[2] == 0x61);
    assert(m.writes[1].data[1] == 0x09 && m.writes[1].data[2] == 0x91);
    assert(m.writes[2].data[1] == 0x15 && m.writes[2].data[2] == 0x91);

    printf("test_set_antennae_updates_ants_from_dc_check_reply OK\n");
}

static void test_set_antennae_runs_rl_test_when_not_all_connected(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    static const uint8_t dc_reply[14] = {0xFF,0x09,0x61,0x00,0x00,0x05,0x01,0x00,0x02,0x00,0x03,0x00,0x04,0x00};
    m.canned_reply = dc_reply;
    m.canned_reply_len = sizeof(dc_reply);

    uhf_reader_set_antennae(&r, UHF_REGION_FCC);

    assert(m.write_count == 6);
    for (int i = 1; i <= 4; i++) {
        assert(m.writes[i].data[1] == 0x16);
    }

    printf("test_set_antennae_runs_rl_test_when_not_all_connected OK\n");
}

/* A well-formed "good" return-loss reply frame for antenna `ant_no`:
   FF 15 AA ... with buf[19]=ant_no and buf[25]=raw>=100 (uhf_parse_
   return_loss treats raw>=100 as good). Frame length is b1+7 = 28;
   the parser does not CRC-check return-loss frames, so the filler
   bytes don't need a valid checksum. */
static void make_good_rl_reply(uint8_t out[28], uint8_t ant_no) {
    memset(out, 0, 28);
    out[0] = 0xFF;
    out[1] = 0x15;
    out[2] = 0xAA;
    out[19] = ant_no;
    out[25] = 200; /* >= 100 -> "good" */
}

/* The DC check and the return-loss test are two INDEPENDENT ways to
   confirm an antenna; a read FAILURE on one must not stop the other
   from confirming a connection. Here the DC-check read errors (-1),
   which the pre-fix code treated as fatal (returned early, ants=0) --
   the return-loss test on antenna 1 must still run and confirm it. */
static void test_rl_confirms_antenna_when_dc_read_fails(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    static uint8_t rl1[28];
    make_good_rl_reply(rl1, 1); /* antenna 1 -> bit 3 (0x08) */

    /* Read-call order in uhf_reader_set_antennae:
       [0] DC check, [1..4] return-loss antennas 1..4, [5] power-set. */
    m.scripts[0].err = 1;                 /* DC-check read fails */
    m.scripts[1].data = rl1;              /* antenna 1 RL: good  */
    m.scripts[1].len = sizeof(rl1);
    /* scripts[2..] left zero -> reads return 0 bytes (no confirm) */
    m.script_len = 6;

    int rc = uhf_reader_set_antennae(&r, UHF_REGION_FCC);

    /* No longer aborts on the failed DC read; return-loss still ran and
       confirmed antenna 1 only. */
    assert(rc == 0);
    assert(r.ants == 0x08);

    /* DC check + 4 return-loss tests + antenna-enable + power-set were
       all still issued (6 writes: the enable is sent because ants != 0). */
    assert(m.writes[0].data[2] == 0x61);              /* DC check */
    assert(m.writes[1].data[1] == 0x16);              /* RL antenna 1 */
    assert(m.writes[4].data[1] == 0x16);              /* RL antenna 4 */

    printf("test_rl_confirms_antenna_when_dc_read_fails OK\n");
}

/* THE ENABLE RULE end to end: a port is turned on iff DC OR return-loss
   passed; a port that fails BOTH is left out of the antenna-enable
   command. Set up antenna 2 passing DC only, antenna 1 passing return-
   loss only, and antennas 3 & 4 failing both -- the enable command (0x91
   0x02) must list exactly ports 1 and 2. */
static void test_both_fail_port_not_enabled(void) {
    mock_uart_t m; mock_reset(&m);
    uhf_transport_t t = make_transport(&m);
    uhf_reader_t r;
    memset(&r, 0, sizeof(r));
    r.transport = &t;

    /* DC status frame: only antenna 2 connected. Per uhf_parse_ant_status,
       buf[7]/[9]/[11]/[13] are the ant1..ant4 connect flags, shifted into
       bits 3..0, so ant2's flag at buf[9]=1 -> mask bit 2 (0x04). */
    static const uint8_t dc_reply[14] =
        {0xFF,0x09,0x61,0x00,0x00,0x05, 0x01,0x00, 0x02,0x01, 0x03,0x00, 0x04,0x00};
    static uint8_t rl1[28];
    make_good_rl_reply(rl1, 1); /* antenna 1 passes return-loss -> bit 3 (0x08) */

    /* Reads: [0]=DC, [1..4]=return-loss ant 1..4, [5]=enable, [6]=power. */
    m.scripts[0].data = dc_reply; m.scripts[0].len = sizeof(dc_reply);
    m.scripts[1].data = rl1;      m.scripts[1].len = sizeof(rl1);
    /* ant 2 return-loss: empty read (ant 2 already confirmed by DC);
       ant 3 & 4: empty read -> fail both DC and return-loss. */
    m.script_len = 8;

    int rc = uhf_reader_set_antennae(&r, UHF_REGION_FCC);
    assert(rc == 0);

    /* Union: DC(ant2 -> 0x04) | RL(ant1 -> 0x08) = 0x0C. Ports 3 & 4 out. */
    assert(r.ants == 0x0C);

    /* The antenna-enable command is write index 5 (DC + 4 RL precede it).
       0x91/0x02 with data length (cnt*2)+1 = 5, listing ports 1 and 2 as
       monostatic pairs (j,j) and NOTHING for the both-fail ports 3 & 4. */
    const uint8_t *en = m.writes[5].data;
    assert(en[1] == 0x05 && en[2] == 0x91 && en[3] == 0x02);
    assert(en[4] == 1 && en[5] == 1);   /* port 1 (RL-only pass) */
    assert(en[6] == 2 && en[7] == 2);   /* port 2 (DC-only pass) */

    printf("test_both_fail_port_not_enabled OK\n");
}

int main(void) {
    test_open_sends_version_query();
    test_start_aborts_with_no_antennas();
    test_start_sends_command_when_antennas_present();
    test_stop_sends_stop_command();
    test_get_temperature_parses_reply();
    test_set_antennae_updates_ants_from_dc_check_reply();
    test_set_antennae_runs_rl_test_when_not_all_connected();
    test_rl_confirms_antenna_when_dc_read_fails();
    test_both_fail_port_not_enabled();
    printf("\nAll uhf_reader orchestration tests passed.\n");
    return 0;
}
