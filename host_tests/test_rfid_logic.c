#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "rfid_logic.h"

/* --- mock file for binary search test ------------------------------ */
static nrf_record_t mock_records[10];
static long mock_pos = 0;

static int mock_seek(void *ctx, long offset) {
    (void)ctx;
    mock_pos = offset;
    return 0;
}
static long mock_read(void *ctx, void *buf, size_t size) {
    (void)ctx;
    long idx = mock_pos / (long)sizeof(nrf_record_t);
    if (idx < 0 || idx >= 10) return -1;
    memcpy(buf, &mock_records[idx], size);
    mock_pos += (long)size;
    return (long)size;
}
static long mock_filesize(void *ctx) {
    (void)ctx;
    return (long)(10 * sizeof(nrf_record_t));
}

int main(void) {
    char buf[128];
    nrf_record_t rec;
    int n;

    /* --- rfid_create_sock_string: LF chip record --- */
    memset(&rec, 0, sizeof(rec));
    memcpy(rec.xpdr_code, "ABC123", 6);
    rec.date_time = 1000;
    rec.ms = 42;
    rec.loop_data = (2 << 6); /* loop_id should come out as 3 */
    rec.max_RSSI = -50;
    rec.wake_count = 1;
    rec.battery = 90;
    rec.log_id = 555;
    n = rfid_create_sock_string(&rec, 0, buf, sizeof(buf), 0, 0, 3, OUTPUT_DEC, 0);
    printf("LF record : [%s] (n=%d)\n", buf, n);
    assert(n > 0);
    assert(strstr(buf, "ABC123") != NULL);

    /* --- UHF record, decimal --- */
    n = rfid_create_sock_string(&rec, 1, buf, sizeof(buf), 123456789UL, 1, 5, OUTPUT_DEC, 0);
    printf("UHF dec   : [%s] (n=%d)\n", buf, n);
    assert(strstr(buf, "123456789") != NULL);

    /* --- UHF record, hex --- */
    n = rfid_create_sock_string(&rec, 1, buf, sizeof(buf), 0xBEEF, 1, 5, OUTPUT_HEX, 0);
    printf("UHF hex   : [%s] (n=%d)\n", buf, n);
    assert(strstr(buf, "BEEF") != NULL);

    /* --- trigger record --- */
    n = rfid_create_sock_string(&rec, 0, buf, sizeof(buf), 0, 0, 7, OUTPUT_DEC, 1);
    printf("Trigger   : [%s] (n=%d)\n", buf, n);
    assert(n > 0);

    /* --- rfid_parse_ip --- */
    uint8_t ip[4];
    assert(rfid_parse_ip("192.168.1.90", ip) == 1);
    assert(ip[0] == 192 && ip[1] == 168 && ip[2] == 1 && ip[3] == 90);
    assert(rfid_parse_ip("192.168.1", ip) == 0);          /* too few octets */
    assert(rfid_parse_ip("192.168.1.90.5", ip) == 0);     /* too many */
    assert(rfid_parse_ip("192.168.1.300", ip) == 0);      /* out of range */
    assert(rfid_parse_ip("", ip) == 0);                    /* empty string */
    /* NOTE: matches original firmware behaviour -- non-numeric tokens are
     * silently atoi()'d to 0 rather than rejected. "not.an.ip.addr" would
     * "successfully" parse to 0.0.0.0 in both the original and this port.
     * Flagged for the developer to decide whether to tighten this on the
     * RT1062 port (recommend adding isdigit() validation per token). */
    assert(rfid_parse_ip("not.an.ip.addr", ip) == 1);
    assert(ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0);
    printf("parse_ip tests OK\n");

    /* --- rfid_round_up_to_max_pow10 --- */
    assert(rfid_round_up_to_max_pow10(47) == 50);
    assert(rfid_round_up_to_max_pow10(123) == 200);
    printf("round_up tests OK (47->%d, 123->%d)\n",
           rfid_round_up_to_max_pow10(47), rfid_round_up_to_max_pow10(123));

    /* --- rfid_binary_search_log --- */
    for (int i = 0; i < 10; i++) {
        memset(&mock_records[i], 0, sizeof(nrf_record_t));
        mock_records[i].date_time = 1000 + (uint32_t)i * 10; /* 1000,1010,...1090 */
        mock_records[i].log_id = (uint32_t)i;
    }
    uint32_t recno = 0xFFFFFFFF;
    int r;

    /* exact match by time */
    r = rfid_binary_search_log(NULL, mock_seek, mock_read, mock_filesize,
                                1050, REWIND_BY_TIME, &recno);
    printf("binsearch exact time=1050 -> r=%d recno=%u\n", r, recno);
    assert(r == 1 && recno == 5);

    /* start_value == 0 -> full rewind */
    r = rfid_binary_search_log(NULL, mock_seek, mock_read, mock_filesize,
                                0, REWIND_BY_TIME, &recno);
    assert(r == 1 && recno == 0);

    /* start_value beyond last record -> nothing to rewind */
    r = rfid_binary_search_log(NULL, mock_seek, mock_read, mock_filesize,
                                999999, REWIND_BY_TIME, &recno);
    printf("binsearch beyond end -> r=%d\n", r);
    assert(r == 0);

    /* by record number (log_id) */
    r = rfid_binary_search_log(NULL, mock_seek, mock_read, mock_filesize,
                                3, REWIND_BY_RECNO, &recno);
    assert(r == 1 && recno == 3);

    printf("\nAll tests passed.\n");
    return 0;
}
