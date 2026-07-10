#include <stdio.h>
#include <assert.h>
#include "ms_time.h"

static void test_normal_case(void) {
    assert(ms_elapsed(1500, 1000) == 500);
    assert(ms_has_elapsed(1500, 1000, 500) == 1);
    assert(ms_has_elapsed(1500, 1000, 501) == 0);
    assert(ms_has_elapsed(1500, 1000, 499) == 1);
    printf("test_normal_case OK\n");
}

static void test_zero_elapsed(void) {
    assert(ms_elapsed(1000, 1000) == 0);
    assert(ms_has_elapsed(1000, 1000, 0) == 1);
    assert(ms_has_elapsed(1000, 1000, 1) == 0);
    printf("test_zero_elapsed OK\n");
}

static void test_wraparound(void) {
    uint32_t since = 0xFFFFFF00u;
    uint32_t now = 100u;
    uint32_t expected_elapsed = 0x100u + 100u;

    assert(ms_elapsed(now, since) == expected_elapsed);
    assert(ms_has_elapsed(now, since, 300) == 1);
    assert(ms_has_elapsed(now, since, 400) == 0);
    printf("test_wraparound OK (elapsed=%u)\n", ms_elapsed(now, since));
}

static void test_the_original_underflow_bug_is_actually_fixed(void) {
    uint32_t boot_time = 0;
    uint32_t now = 50;
    uint32_t beep_delay = 300;

    assert(ms_has_elapsed(now, boot_time, beep_delay) == 0);

    now = 350;
    assert(ms_has_elapsed(now, boot_time, beep_delay) == 1);

    printf("test_the_original_underflow_bug_is_actually_fixed OK\n");
}

int main(void) {
    test_normal_case();
    test_zero_elapsed();
    test_wraparound();
    test_the_original_underflow_bug_is_actually_fixed();
    printf("\nAll ms_time tests passed.\n");
    return 0;
}
