/*
 * finish_lynx_protocol.h
 *
 * Ported from FINISHLYNX.LIB -- pure string-building logic for the
 * FinishLynx race-timing protocol (SOH-prefixed lines over TCP,
 * port 10001). No I/O here -- RTC reads, socket writes, and timing
 * live in finish_lynx_reader.h's orchestration layer.
 */

#ifndef FINISH_LYNX_PROTOCOL_H
#define FINISH_LYNX_PROTOCOL_H

#include "nrf_record.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FINISH_LYNX_PORT 10001

/*
 * Was Make_FinishLynx_Time_String()'s sprintf: "\x01T,HH:MM:SS.mmm\r\n".
 * Pure formatting only -- the RTC read and the
 * MS_TIMER-minus-iDSTimeFromRabbit millisecond computation both happen
 * at the call site (see finish_lynx_reader.h), not here. Returns the
 * string length (like sprintf), or -1 if out_size is too small.
 */
int finish_lynx_build_time_string(int hour, int min, int sec, int ms, char *out, size_t out_size);

/*
 * Was CreateSockString_FinishLynx(): "\x01S,HH:MM:SS.mmm,XPDRCODE\r\n".
 * is_rewind is UNUSED in the original function body too (accepted as
 * a parameter but never referenced anywhere inside it) -- preserved
 * here for signature fidelity rather than silently dropped, and
 * explicitly marked unused in the implementation. XPDRCODE is
 * rec->xpdr_code truncated at 6 bytes or the first embedded NUL,
 * whichever comes first (was the original's "%.6s" on a
 * non-NUL-terminated 6-byte field).
 */
int finish_lynx_build_split_string(const nrf_record_t *rec, int is_rewind, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* FINISH_LYNX_PROTOCOL_H */
