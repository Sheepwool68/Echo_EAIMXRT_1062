/*
 * app_pc_dispatch.h
 *
 * The dispatch logic that CheckForPCCommands() had inline in the
 * original, now separated: pc_protocol.h classifies *what* a command
 * is, this module decides *what to do* about it, calling into the
 * already-ported modules (nand_log, uhf_reader, ds3231, etc).
 *
 * Same honest scope note as uhf_reader.c/gprs_modem.c: this is
 * integration glue, not a pure algorithm, so it's less unit-testable
 * than the protocol modules it calls into. A few commands here also
 * touch pieces that aren't ported yet (settings persistence, display) --
 * marked with TODO where that's the case.
 */

#ifndef APP_PC_DISPATCH_H
#define APP_PC_DISPATCH_H

#include "app_context.h"
#include "pc_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Was the big if/else chain in CheckForPCCommands(). socket_index
 * identifies which of the 3 TCP client slots the command arrived on
 * (needed for rewind bookkeeping). reply_transport is that same
 * slot's transport, passed through so commands that reply inline in
 * the original (get time, get reading status, get settings) can still
 * do so directly here, rather than introducing an indirection the
 * original didn't have.
 */
void app_dispatch_pc_command(app_context_t *app, int socket_index,
                              const tcp_socket_transport_t *reply_transport,
                              const pc_parsed_command_t *cmd);

/*
 * Was UHF_Reader_Control(). Starts or stops the UHF reader, including
 * the NAND file-size check/auto-reset (start) and the various flag/
 * display resets (stop). enable: 1 = start, 0 = stop.
 */
/* Was the GENIE_SYSTEM touch-event handler's UHF-on/off toggle -- see
 * app_pc_dispatch.c's doc comment for the exact original logic this
 * ports (a distinct, lower-level operation from
 * app_uhf_reader_control() below: this fully powers the reader
 * hardware up/down and switches the nRF52833's mode, rather than just
 * starting/stopping the scanning loop). */
void app_uhf_active_mode_toggle(app_context_t *app, int uhf_enabled);

void app_uhf_reader_control(app_context_t *app, int enable);

#ifdef __cplusplus
}
#endif

#endif /* APP_PC_DISPATCH_H */
