#include <stdio.h>
#include <assert.h>
#include "ip_addr_parse.h"

static void test_valid_address(void) {
    uint32_t addr = 0;
    int ok = ip_addr_parse_dotted_quad("192.168.1.10", &addr);
    assert(ok == 1);
    assert(addr == 0xC0A8010Au);
    printf("test_valid_address OK\n");
}

static void test_all_zero(void) {
    uint32_t addr = 0xFFFFFFFFu;
    int ok = ip_addr_parse_dotted_quad("0.0.0.0", &addr);
    assert(ok == 1);
    assert(addr == 0);
    printf("test_all_zero OK\n");
}

static void test_all_255(void) {
    uint32_t addr = 0;
    int ok = ip_addr_parse_dotted_quad("255.255.255.255", &addr);
    assert(ok == 1);
    assert(addr == 0xFFFFFFFFu);
    printf("test_all_255 OK\n");
}

static void test_rejects_out_of_range_octet(void) {
    uint32_t addr;
    assert(ip_addr_parse_dotted_quad("256.1.1.1", &addr) == 0);
    assert(ip_addr_parse_dotted_quad("1.1.1.999", &addr) == 0);
    printf("test_rejects_out_of_range_octet OK\n");
}

static void test_rejects_too_few_octets(void) {
    uint32_t addr;
    assert(ip_addr_parse_dotted_quad("192.168.1", &addr) == 0);
    printf("test_rejects_too_few_octets OK\n");
}

static void test_rejects_trailing_garbage(void) {
    uint32_t addr;
    assert(ip_addr_parse_dotted_quad("192.168.1.1extra", &addr) == 0);
    assert(ip_addr_parse_dotted_quad("192.168.1.1.", &addr) == 0);
    printf("test_rejects_trailing_garbage OK\n");
}

static void test_rejects_empty_segment(void) {
    uint32_t addr;
    assert(ip_addr_parse_dotted_quad("192..1.1", &addr) == 0);
    assert(ip_addr_parse_dotted_quad(".192.1.1", &addr) == 0);
    printf("test_rejects_empty_segment OK\n");
}

static void test_rejects_null(void) {
    uint32_t addr;
    assert(ip_addr_parse_dotted_quad(NULL, &addr) == 0);
    printf("test_rejects_null OK\n");
}

int main(void) {
    test_valid_address();
    test_all_zero();
    test_all_255();
    test_rejects_out_of_range_octet();
    test_rejects_too_few_octets();
    test_rejects_trailing_garbage();
    test_rejects_empty_segment();
    test_rejects_null();
    printf("\nAll ip_addr_parse tests passed.\n");
    return 0;
}
