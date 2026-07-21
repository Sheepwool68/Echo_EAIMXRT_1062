/*
 * uhf_reader.h
 *
 * Orchestration layer tying uhf_commands.h (what to send),
 * uhf_transport.h (how to send/receive it), and uhf_packet_parser.h
 * (how to interpret replies) together -- was Open_Reader(),
 * TM_SetAntennae(), TM_InitialiseReader(), StartReaders(),
 * StopReaders(), GetReaderTemp().
 *
 * HONEST SCOPE NOTE: unlike the pure logic modules in this port (CRC,
 * chip array, packet parser, command builders -- all exhaustively unit
 * tested), this layer is fundamentally I/O choreography: send this,
 * wait N ms, read a reply, send the next thing. That's much harder to
 * meaningfully unit test (a mock can confirm the right bytes were sent
 * in the right order, but not that the timing actually works against
 * real reader firmware). I've preserved every delay/timeout value from
 * the original exactly, with a comment at each one, and tested the
 * command SEQUENCING against a mock transport -- but I'd treat this
 * layer as needing real hardware integration testing before trusting
 * it, more so than the rest of this port.
 *
 * FLAGGED ASYMMETRY: in TM_InitialiseReader's region-frequency step,
 * the original waits (msDelay + read) after sending for EU and AU, but
 * NOT for China/EU-high/Indonesia/FCC -- those commands are sent and
 * the code moves straight on to the next step with no reply read.
 * Preserved exactly as asymmetric; flagged in case it wasn't
 * intentional, but not silently "fixed" since I don't know whether
 * those regions' commands genuinely don't need a reply.
 */

#ifndef UHF_READER_H
#define UHF_READER_H

#include "uhf_transport.h"
#include "uhf_commands.h"
#include "uhf_packet_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uhf_transport_t *transport;
    uint8_t ants;           /* connected-antenna bitmask, was global `ants` */
    uint32_t duty_cycle;    /* was global `duty_cycle` */
} uhf_reader_t;

/* Was Open_Reader(): opens the UART, flushes, sends the version query. */
int uhf_reader_open(uhf_reader_t *r, const uhf_transport_t *t);

/*
 * Was TM_SetAntennae(): DC-connection check, optional per-antenna
 * return-loss test (only run if the DC check didn't already show all
 * 4 antennas connected -- matches `if(ants != 0x0F)`), antenna-enable,
 * and power-set. Updates r->ants and r->duty_cycle as a side effect,
 * same as the original's global variable updates.
 */
int uhf_reader_set_antennae(uhf_reader_t *r, uhf_region_t region);

/*
 * Was TM_InitialiseReader(): full configuration sequence (program
 * info, firmware boot mode, region list, region/frequency, Q
 * algorithm, target mode/session/RF mode) followed by
 * uhf_reader_set_antennae().
 */
int uhf_reader_initialise(uhf_reader_t *r, uhf_region_t region,
                           uint8_t channel, int uhf_mode);

/*
 * Was StartReaders(): begins asynchronous inventory (continuous tag
 * streaming). Matches the original's behavior of silently aborting
 * (returning without sending anything) if r->ants == 0 -- returns 0 in
 * that case rather than 1, so the caller can tell the difference
 * between "started" and "no antennas, didn't start."
 */
int uhf_reader_start(uhf_reader_t *r, int heartbeat_enabled);

/* Was StopReaders(): stops inventory. Originally also queried
 * temperature as a second command -- DISABLED 2026-07-17, see
 * uhf_reader.c's own comment at that call site (the reader doesn't
 * reliably answer a temperature query immediately after stop_reading,
 * only when genuinely idle, and it isn't currently used). */
int uhf_reader_stop(uhf_reader_t *r);

/* Was GetReaderTemp() (also the second half of StopReaders()). If the
 * reply contains a parseable temperature frame, *out_temp is set and
 * 1 is returned; pass NULL if you don't need the value. Returns 0 if
 * the command was sent but no temperature frame was found in the
 * reply, negative on a transport error. */
int uhf_reader_get_temperature(uhf_reader_t *r, int *out_temp);

#ifdef __cplusplus
}
#endif

#endif /* UHF_READER_H */
