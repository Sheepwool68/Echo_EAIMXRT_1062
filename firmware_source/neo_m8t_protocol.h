/*
 * neo_m8t_protocol.h
 *
 * Pure logic ported from NEOM8T.LIB (u-blox NEO-M8T GPS module, UBX
 * binary protocol over SPI). Checksum, sync-byte scanning, ACK/NAK
 * classification, NAV-PVT field extraction, and coordinate string
 * formatting -- no SPI I/O here, that's neo_m8t_reader.h's job.
 *
 * *** IMPORTANT PRESERVED QUIRK -- READ BEFORE CHANGING VALIDITY LOGIC ***
 * The original's fix-validity check was:
 *   if((s[17] & 7 == 7) && (s[26]>0) && (s[27] & 0x01 > 0))
 * C's == and > bind tighter than &, so this actually evaluates as
 * (s[17]&1) && (s[26]>0) && (s[27]&1) -- NOT (s[17]&7)==7 as the
 * "& 7 == 7" phrasing suggests at a glance. Concretely: s[17] is
 * NAV-PVT's 'valid' byte (bit0=validDate, bit1=validTime,
 * bit2=fullyResolved); only validDate ends up checked, not all three
 * bits together. ubx_parse_nav_pvt() below reproduces this exactly
 * (status = validDate bit only), not the likely-intended stricter
 * check -- silently tightening GPS fix-validity criteria isn't this
 * port's call to make. The second affected term (s[27]&0x01>0) is a
 * coincidentally harmless instance of the same mistake (numerically
 * identical either way), so it's not a behavior change.
 */

#ifndef NEO_M8T_PROTOCOL_H
#define NEO_M8T_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UBX_SYNC1 0xB5
#define UBX_SYNC2 0x62

typedef struct {
    int status;   /* validity per the preserved quirk above -- see header comment */
    int year;
    int month;
    int day;
    int hours;
    int minutes;
    int seconds;
    int sats;
    int32_t lat;  /* 1e-7 degrees */
    int32_t lon;  /* 1e-7 degrees */
} neo_pvt_t;

/*
 * Was the checksum loop inside process_UBX(): Fletcher-8-style
 * accumulator over bytes[2 .. length-3] inclusive (everything after
 * the 2 sync bytes, before the 2 checksum bytes), writing the result
 * into buf[length-2]/buf[length-1].
 */
void ubx_compute_checksum(uint8_t *buf, size_t length);

/*
 * Was the `if (s[0]==0xB5 && s[1]==0x62)` scan inside process_UBX()'s
 * response loop. Returns the byte offset of the first sync pair found
 * in buf[0..buf_len), or -1 if none.
 */
int ubx_find_sync(const uint8_t *buf, size_t buf_len);

/*
 * Was `if(s[3]==0x01) return 1; if(s[3]==0x00) return 0;` (the
 * message_type==0 / config-ack path). msg_at_sync must point at the
 * 0xB5 sync byte, with at least 4 bytes available after it. Returns
 * 1=ACK, 0=NAK, -1=neither (caller should keep scanning/waiting).
 */
int ubx_classify_ack(const uint8_t *msg_at_sync);

/*
 * Was `s[2]==0x01 && s[3]==0x07 && s[4]==0x5C` -- checks whether the
 * message at msg_at_sync (pointing at the 0xB5 sync byte) is a
 * NAV-PVT reply. NOTE: preserves the original's own incompleteness --
 * it checks only the length's LOW byte (0x5C = 92 decimal, NAV-PVT's
 * actual length) and never checks the high byte, which should be 0x00.
 * msg_at_sync must have at least 5 bytes available.
 */
int ubx_is_nav_pvt(const uint8_t *msg_at_sync);

/*
 * Was the field-extraction block for message_type==1. msg_at_sync
 * must point at the 0xB5 sync byte with at least 38 bytes available
 * (matches the original's use of offsets up to s[37]). See this
 * header's top comment for the preserved validity-check quirk.
 */
void ubx_parse_nav_pvt(const uint8_t *msg_at_sync, neo_pvt_t *out);

/*
 * Was NMEA() -- formats lat/lon (1e-7 degree units) as
 * "DDMM.MMMMM,N,DDDMM.MMMMM,E" (degrees + minutes with 5 decimal
 * places of minutes, standard NMEA style). out_size must be at least
 * 35 bytes (matches the original's fixed GPSCoords buffer). Returns
 * the number of characters written (excluding NUL), or -1 if out_size
 * is too small.
 */
int neo_format_nmea(int32_t lat, int32_t lon, char *out, size_t out_size);

/*
 * Was DD_DMS() -- decimal-degrees format "D.DDDDDDD,N,DDD.DDDDDDD,E".
 * Not called from anywhere in the pasted source (dead code kept for
 * reference in the original) -- ported for fidelity, not because
 * anything currently calls it.
 */
int neo_format_dd_dms(int32_t lat, int32_t lon, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* NEO_M8T_PROTOCOL_H */
