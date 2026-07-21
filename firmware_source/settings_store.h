/*
 * settings_store.h
 *
 * Was LoadSettings()/SaveSettings() -- the original used Rabbit's
 * userblock flash region (writeUserBlockArray/readUserBlockArray),
 * whose actual implementation wasn't available to port (a Dynamic C
 * BIOS primitive, not something in any library pasted so far). This
 * is new infrastructure: persists device_settings_t as a small file
 * within the SAME littlefs filesystem already used for the record log
 * (reuses nand_log_t's embedded `lfs_t` directly -- see the call sites
 * in app_init.c/app_pc_dispatch.c, which pass &app->log.lfs).
 *
 * SAFETY DESIGN (new, not inherited from anything -- there was nothing
 * to port here beyond "persist a struct"):
 *   - A small header (magic + version + declared size + CRC32) is
 *     written before the settings blob. On load, all four are checked
 *     before the blob is trusted -- protects against a firmware update
 *     that changes device_settings_t's layout silently misinterpreting
 *     an old on-disk blob, and against a partially-written file from a
 *     power loss mid-save.
 *   - Reuses crc32.h, already built and tested for the OTA download
 *     path -- no new CRC implementation needed here.
 *   - The header/blob split is pure logic (settings_store_build_header/
 *     settings_store_validate), fully unit-testable without real
 *     flash/littlefs; only settings_store_load/save touch lfs_t.
 *
 * CALLER RESPONSIBILITY, not solved here: don't call
 * settings_store_save() on every single setting change if you can
 * avoid it -- NOR flash has a finite erase-cycle life. Batch changes
 * or save on a periodic/idle timer where your use case allows it.
 */

#ifndef SETTINGS_STORE_H
#define SETTINGS_STORE_H

#include <stdint.h>
#include <stddef.h>
#include "lfs.h"
#include "nrf_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_STORE_PATH "/settings.dat"
#define SETTINGS_STORE_MAGIC   0x53455454u
/* Bumped 2->3, 2026-07-20: same mechanism as the 1->2 bump above, same
 * root cause class. rabbit_ip's real default (192.168.1.90, the
 * "CRITICAL FIX 2026-07-16" in app_init.c) was added to the in-code
 * defaults AFTER this device had already saved a real settings.dat
 * with rabbit_ip all-zero (from before that fix existed) -- confirmed
 * on real hardware: the Network form's GENIE_LAN_STR showed 0.0.0.0
 * despite the static-IP branch and the corrected default both being
 * verified correct by direct code review. Nothing in this port ever
 * writes rabbit_ip back to a different value at runtime, so the stale
 * zero just kept getting silently reloaded and re-persisted forever.
 * Bumping the version forces that old file to fail validation on next
 * boot, same trade-off as before: clears every other saved setting
 * too (reader_power, channel, System mode, beeper, etc.), accepted
 * explicitly by the user rather than writing a rabbit_ip-only targeted
 * fix.
 *
 * Bumped 3->4, 2026-07-20, same day: the 2->3 bump alone didn't fix it
 * -- settings_store_load() had a SEPARATE bug at the time (fixed
 * separately, see settings_store.c) that read the file's raw bytes
 * into the caller's live app->settings BEFORE validating, so the very
 * first boot on v3 loaded the still-present v2 file's zeroed rabbit_ip
 * into app->settings, got a correct "invalid" verdict a moment later,
 * but then saved THAT ALREADY-CORRUPTED value as a brand-new, genuinely
 * self-consistent v3 file (right version, right CRC, just wrong data).
 * Confirmed on real hardware via the boot-time trace (app_init.c):
 * `Settings: load OK (existing file valid), rabbit_ip=0.0.0.0`. The
 * settings_store_load() bug is now fixed too, so THIS bump is the
 * "for real this time" one -- rejects that bad v3 file, and with the
 * load bug gone, the fallback save will actually persist the correct
 * defaults. Same full-reset trade-off as every previous bump. */
#define SETTINGS_STORE_VERSION 4u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t crc32;
} settings_store_header_t;
#pragma pack(pop)

void settings_store_build_header(settings_store_header_t *out_hdr,
                                  const void *settings_data, size_t settings_size);

int settings_store_validate(const settings_store_header_t *hdr,
                             const void *settings_data, size_t settings_size);

int settings_store_load(lfs_t *lfs, device_settings_t *out);

int settings_store_save(lfs_t *lfs, const device_settings_t *settings);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_STORE_H */
