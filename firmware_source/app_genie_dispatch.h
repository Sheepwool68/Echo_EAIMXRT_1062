/*
 * app_genie_dispatch.h
 *
 * Was myGenieEventHandler() -- the touchscreen UI's touch-event
 * dispatcher (form navigation, settings changes via knob/trackbar/
 * slider/4Dbutton/winbutton, on-screen keyboard/keypad text entry,
 * PIN-gated admin mode). Sibling to app_pc_dispatch.c: that module
 * decides what to do about a PC-protocol command, this one decides
 * what to do about a touchscreen event -- same "classify elsewhere,
 * act here" split, same integration-glue honesty (this calls into
 * already-ported pure-logic modules; it isn't one itself).
 *
 * SCOPE, decided explicitly rather than guessed: two pieces the
 * original calls into (UpdateRabbitIP()'s blocking Dynamic-C ifconfig()
 * calls, and check_fw()/install_firmware()'s Rabbit-only httpc/
 * buDownload libraries) have no RT1062 equivalent yet and are
 * deliberately STUBBED here -- flagged TODOs, not invented behavior.
 * The firmware-update pieces are additionally superseded in this port
 * by fw_version_check.h/fw_downloader.h/fw_install_mcuboot.h (built for
 * MCUboot, not Rabbit's own updater) -- wiring those together with a
 * real HTTP transport is a separate task.
 */

#ifndef APP_GENIE_DISPATCH_H
#define APP_GENIE_DISPATCH_H

#include "app_context.h"
#include "display_stub.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Was the big if/else chain in myGenieEventHandler() that runs when
 * Event.reportObject.cmd == GENIE_REPORT_EVENT (a genuine touch/
 * interaction, not a connect/ping status change -- display_stub.c's
 * display_dequeue_event() already filters those out before this is
 * ever called). Call once per event dequeued from app_loop.c's
 * process_display_events(), which owns draining the queue -- this
 * function only reacts to one event already pulled out, same
 * "not the queue owner" convention as app_pc_dispatch.c.
 */
void app_dispatch_genie_event(app_context_t *app, const display_event_t *ev);

/*
 * Was updateGenie_Main() -- refreshes the Main form's widgets from
 * current settings/state (clock digits, date, channel/power readouts,
 * Active/UHF mode strings, battery gauge) and applies any pending
 * timezone offset (app->time_offset) to the RTC. Exported since
 * app_dispatch_genie_event() calls it both on FORM_MAIN entry and after
 * UHF_Reader_Control()-equivalent actions, matching the original's own
 * multiple call sites.
 */
void app_genie_update_main(app_context_t *app);

/*
 * Was updateNetworkStrings() -- refreshes the Networking form's IP/port/
 * APN/MAC text fields and DHCP/Remote 4Dbutton state from current
 * settings (and, when DHCP is on, the live lwIP-assigned address via
 * enet_lwip_rt1062_get_ip()).
 */
void app_genie_update_network_strings(app_context_t *app);

#ifdef __cplusplus
}
#endif

#endif /* APP_GENIE_DISPATCH_H */
