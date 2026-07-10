/*
 * ip_addr_parse.h
 *
 * Was resolve() as actually used at ConnectToSocketServer()'s call
 * site -- that call always passes a dotted-quad string built from 4
 * numeric octets (Settings.GPRSServerIP1), never a hostname, so a full
 * DNS resolver isn't needed for this specific use. If you later have a
 * call site that needs to resolve an actual hostname, that's a
 * separate, not-yet-built piece (lwIP's dns_gethostbyname(), async
 * callback-based under the raw API) -- flagged as a gap, not silently
 * assumed unnecessary in general.
 */

#ifndef IP_ADDR_PARSE_H
#define IP_ADDR_PARSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parses a dotted-quad string ("a.b.c.d") into a big-endian uint32_t
 * (byte order matching what you'd hand to lwIP's ip_addr_set_ip4_u32,
 * i.e. octet 'a' in the most significant byte). Returns 1 on success,
 * 0 if the string isn't a valid dotted-quad (matches resolve()'s "0
 * means failure" convention at the call site, which checks
 * `iIPAddress == 0`).
 */
int ip_addr_parse_dotted_quad(const char *s, uint32_t *out_addr);

#ifdef __cplusplus
}
#endif

#endif /* IP_ADDR_PARSE_H */
