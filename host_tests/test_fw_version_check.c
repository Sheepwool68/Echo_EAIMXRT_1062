#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "fw_version_check.h"

static void test_parse_html_hex_version(void) {
    const char *body = "<html><h1>0x0142</h1></html>";
    uint16_t ver = 0;
    int ok = fw_parse_version_html(body, strlen(body), 30, &ver);
    assert(ok == 1);
    assert(ver == 0x0142);
    printf("test_parse_html_hex_version OK\n");
}

static void test_parse_html_decimal_version(void) {
    const char *body = "<h1>4200  </h1>"; /* no leading zero -- see gotcha test below */
    uint16_t ver = 0;
    int ok = fw_parse_version_html(body, strlen(body), 30, &ver);
    assert(ok == 1);
    assert(ver == 4200);
    printf("test_parse_html_decimal_version OK\n");
}

static void test_parse_html_leading_zero_is_octal_not_decimal(void) {
    /* FLAGGED GOTCHA, inherited faithfully from the original: strtol()
     * with base 0 treats a leading "0" (not "0x") as an OCTAL prefix.
     * "000322" is therefore parsed as octal 322 = decimal 210, NOT
     * decimal 322. This landmine exists in the ORIGINAL firmware too
     * (same `strtol(fw_str, NULL, 0)` call) -- if your version-check
     * page ever emits a zero-padded decimal version number, both the
     * original and this port silently misread it. Documented here
     * rather than silently "fixed," since changing it changes what a
     * real version string on your actual check page means. */
    const char *body = "<h1>000322</h1>";
    uint16_t ver = 0;
    int ok = fw_parse_version_html(body, strlen(body), 30, &ver);
    assert(ok == 1);
    assert(ver == 210); /* octal "000322" == decimal 210 -- NOT 322.
                            (Written as decimal 210 here deliberately,
                            not "0210", which would be octal in C source too!) */
    printf("test_parse_html_leading_zero_is_octal_not_decimal OK (gotcha confirmed, matches original)\n");
}

static void test_parse_html_marker_beyond_scan_window(void) {
    /* marker exists but past the 30-char scan bound -- original returns
     * error rather than scanning the whole buffer */
    char body[64];
    memset(body, 'x', sizeof(body));
    memcpy(&body[40], "<h1>012345</h1>", 15);
    uint16_t ver = 0;
    int ok = fw_parse_version_html(body, sizeof(body), 30, &ver);
    assert(ok == 0);
    printf("test_parse_html_marker_beyond_scan_window OK\n");
}

static void test_parse_html_marker_not_present(void) {
    const char *body = "<html><body>no version here</body></html>";
    uint16_t ver = 0;
    int ok = fw_parse_version_html(body, strlen(body), 30, &ver);
    assert(ok == 0);
    printf("test_parse_html_marker_not_present OK\n");
}

static void test_parse_html_truncated_after_marker(void) {
    /* marker present but fewer than 6 chars follow -- must not read OOB */
    const char *body = "<h1>12";
    uint16_t ver = 0;
    int ok = fw_parse_version_html(body, strlen(body), 30, &ver);
    assert(ok == 0);
    printf("test_parse_html_truncated_after_marker OK\n");
}

static void test_parse_plain_decimal(void) {
    uint16_t ver = 0;
    int ok = fw_parse_version_plain("322", 3, &ver);
    assert(ok == 1);
    assert(ver == 322);
    printf("test_parse_plain_decimal OK\n");
}

static void test_parse_plain_hex(void) {
    uint16_t ver = 0;
    int ok = fw_parse_version_plain("0x0142", 6, &ver);
    assert(ok == 1);
    assert(ver == 0x0142);
    printf("test_parse_plain_hex OK\n");
}

static void test_compare_versions(void) {
    assert(fw_compare_versions(0x0142, 0x0142) == FW_CHECK_UP_TO_DATE);
    assert(fw_compare_versions(0x0142, 0x0143) == FW_CHECK_UPDATE_AVAILABLE);
    /* flagged original behavior: a LOWER remote version also reports
     * "update available", since it's exact-match-or-not, not a
     * greater-than check */
    assert(fw_compare_versions(0x0143, 0x0142) == FW_CHECK_UPDATE_AVAILABLE);
    printf("test_compare_versions OK\n");
}

int main(void) {
    test_parse_html_hex_version();
    test_parse_html_decimal_version();
    test_parse_html_leading_zero_is_octal_not_decimal();
    test_parse_html_marker_beyond_scan_window();
    test_parse_html_marker_not_present();
    test_parse_html_truncated_after_marker();
    test_parse_plain_decimal();
    test_parse_plain_hex();
    test_compare_versions();
    printf("\nAll fw_version_check tests passed.\n");
    return 0;
}
