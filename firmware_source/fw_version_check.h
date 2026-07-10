/*
 * fw_version_check.h
 *
 * Ported from ActiveRFID.C's check_fw(): the version-comparison logic
 * only. HTTP I/O is a separate transport layer (fw_update_transport.h),
 * consistent with every other module in this port.
 *
 * FLAGGED FRAGILITY (preserved, not silently fixed): the original scans
 * for a literal "<h1>" substring and reads exactly the next 6 characters
 * as the version string. This assumes the version page's HTML never
 * changes shape and that "<h1>VVVVVV</h1>" (or similar) lands within
 * whatever's read first. Ported faithfully since changing the actual
 * version-check page's format is your call, not mine -- but if you're
 * setting up a NEW check endpoint for this port anyway, a small JSON or
 * plain-text response ("142" or {"version":"0x0142"}) would be far more
 * robust than scraping HTML. Both parsers are provided below; pick
 * whichever matches what you actually serve.
 */

#ifndef FW_VERSION_CHECK_H
#define FW_VERSION_CHECK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FW_CHECK_UP_TO_DATE = 0,
    FW_CHECK_UPDATE_AVAILABLE = 1,
    FW_CHECK_NOT_FOUND = -1, /* marker not found within the scan window --
                                 was the original's implicit error path */
} fw_check_result_t;

/*
 * Was the "<h1>" scanning logic in check_fw(). Scans up to
 * max_scan_chars into body for the literal marker, matching the
 * original's `if(n > 30) return -1` bound. Parses the 6 characters
 * immediately following the marker via strtol base 0 (so both
 * "0x0142" and plain decimal work, matching the original exactly).
 * Returns 1 and sets *out_version if found, 0 if not found within the
 * scan window.
 */
int fw_parse_version_html(const char *body, size_t body_len,
                           size_t max_scan_chars, uint16_t *out_version);

/*
 * Alternative, more robust parser for a plain-text response body that's
 * JUST the version number (decimal or 0x-prefixed hex), no HTML
 * scraping required. Recommended if you're standing up a new check
 * endpoint rather than preserving the original's page format.
 */
int fw_parse_version_plain(const char *body, size_t body_len, uint16_t *out_version);

/*
 * Was the `if(_FIRMWARE_VERSION_ == new_fw_vers) return 0; return 1;`
 * comparison. Kept as a one-line named function rather than inlined,
 * so the "what counts as an update" rule is documented and testable in
 * one place.
 *
 * FLAGGED: this is exact-match-or-not, matching the original -- it does
 * NOT check whether the remote version is actually NEWER, just
 * different. A remote version LOWER than current would still report
 * FW_CHECK_UPDATE_AVAILABLE, i.e. this can't distinguish "update" from
 * "accidental downgrade." Preserved as-is since changing it changes
 * what your fleet does when someone points the check URL at stale
 * content; flag if you'd rather only trigger on a strictly greater
 * version number (probably worth doing before this goes to real
 * hardware in the field, but that's your call).
 */
fw_check_result_t fw_compare_versions(uint16_t current_version, uint16_t remote_version);

#ifdef __cplusplus
}
#endif

#endif /* FW_VERSION_CHECK_H */
