#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "settings_store.h"

/* This test only exercises the PURE header build/validate logic, not
 * settings_store_load/save -- but those functions are still compiled
 * into this translation unit and reference the real littlefs
 * functions, so minimal link-satisfying stubs are needed here. Not
 * used by any test below. */
int lfs_file_open(lfs_t *lfs, lfs_file_t *file, const char *path, int flags) {
    (void)lfs; (void)file; (void)path; (void)flags; return -1;
}
int lfs_file_close(lfs_t *lfs, lfs_file_t *file) { (void)lfs; (void)file; return 0; }
lfs_ssize_t lfs_file_read(lfs_t *lfs, lfs_file_t *file, void *buffer, lfs_size_t size) {
    (void)lfs; (void)file; (void)buffer; (void)size; return 0;
}
lfs_ssize_t lfs_file_write(lfs_t *lfs, lfs_file_t *file, const void *buffer, lfs_size_t size) {
    (void)lfs; (void)file; (void)buffer; (void)size; return 0;
}

static void test_build_then_validate_roundtrip(void) {
    device_settings_t s;
    settings_store_header_t hdr;

    memset(&s, 0, sizeof(s));
    s.reader_power = 80;
    s.channel = 3;
    s.time_zone = 8;

    settings_store_build_header(&hdr, &s, sizeof(s));

    assert(hdr.magic == SETTINGS_STORE_MAGIC);
    assert(hdr.version == SETTINGS_STORE_VERSION);
    assert(hdr.size == sizeof(s));
    assert(settings_store_validate(&hdr, &s, sizeof(s)) == 1);

    printf("test_build_then_validate_roundtrip OK\n");
}

static void test_validate_rejects_wrong_magic(void) {
    device_settings_t s;
    settings_store_header_t hdr;
    memset(&s, 0, sizeof(s));

    settings_store_build_header(&hdr, &s, sizeof(s));
    hdr.magic = 0xDEADBEEFu;

    assert(settings_store_validate(&hdr, &s, sizeof(s)) == 0);
    printf("test_validate_rejects_wrong_magic OK\n");
}

static void test_validate_rejects_wrong_version(void) {
    device_settings_t s;
    settings_store_header_t hdr;
    memset(&s, 0, sizeof(s));

    settings_store_build_header(&hdr, &s, sizeof(s));
    hdr.version = SETTINGS_STORE_VERSION + 1;

    assert(settings_store_validate(&hdr, &s, sizeof(s)) == 0);
    printf("test_validate_rejects_wrong_version OK\n");
}

static void test_validate_rejects_wrong_size(void) {
    device_settings_t s;
    settings_store_header_t hdr;
    memset(&s, 0, sizeof(s));

    settings_store_build_header(&hdr, &s, sizeof(s));
    hdr.size = (uint16_t)(sizeof(s) - 1);

    assert(settings_store_validate(&hdr, &s, sizeof(s)) == 0);
    printf("test_validate_rejects_wrong_size OK\n");
}

static void test_validate_rejects_corrupted_data(void) {
    device_settings_t s;
    settings_store_header_t hdr;
    memset(&s, 0, sizeof(s));
    s.channel = 5;

    settings_store_build_header(&hdr, &s, sizeof(s));

    s.channel = 6;

    assert(settings_store_validate(&hdr, &s, sizeof(s)) == 0);
    printf("test_validate_rejects_corrupted_data OK\n");
}

static void test_validate_accepts_all_zero_settings(void) {
    device_settings_t s;
    settings_store_header_t hdr;
    memset(&s, 0, sizeof(s));

    settings_store_build_header(&hdr, &s, sizeof(s));
    assert(settings_store_validate(&hdr, &s, sizeof(s)) == 1);

    printf("test_validate_accepts_all_zero_settings OK\n");
}

int main(void) {
    test_build_then_validate_roundtrip();
    test_validate_rejects_wrong_magic();
    test_validate_rejects_wrong_version();
    test_validate_rejects_wrong_size();
    test_validate_rejects_corrupted_data();
    test_validate_accepts_all_zero_settings();
    printf("\nAll settings_store tests passed.\n");
    return 0;
}
