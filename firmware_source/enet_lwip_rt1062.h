/*
 * enet_lwip_rt1062.h
 *
 * ENET (KSZ8081, RMII) + lwIP raw-API bring-up for the real app, adapted
 * from a standalone bring-up test that was built and confirmed working
 * on real hardware 2026-07-14 (link-up, DHCP, and a TCP echo server all
 * verified) -- see project memory (project_enet_lwip_bringup.md, in the
 * session's memory store, not this repo) for the full history, including
 * a long link-up failure whose root cause was a MISSING COMPILER DEFINE.
 *
 * ***************************************************************
 * INTEGRATION REQUIREMENT, do not skip: your project's build must
 * define FSL_FEATURE_PHYKSZ8081_USE_RMII50M_MODE (a plain -D flag,
 * no value needed). Without it, fsl_phyksz8081.c's PHY_KSZ8081_Init()
 * silently skips a block that configures the KSZ8081 PHY's own
 * PHY_CONTROL2_REG for correct RMII 50MHz reference-clock operation --
 * everything else (pins, clock direction, MDIO, PHY chip-ID read) will
 * appear to work fine, but the physical link will never come up. This
 * cost an entire debugging session to find; add the define FIRST.
 *
 * SECOND REQUIREMENT, found 2026-07-16: also define
 * ENET_MDIO_TIMEOUT_COUNT (e.g. =100000, a plain iteration count, not
 * time-based). Without it, fsl_enet.c's ENET_MDIOWaitTransferOver()
 * falls back to a genuinely UNBOUNDED `while` loop with no timeout at
 * all, waiting forever if a single MDIO transaction's completion
 * interrupt bit never sets -- this hung app_init() completely (silent,
 * indefinite, confirmed via boot-progress checkpoints bracketing it to
 * inside PHY_Init()/ENET_MDIORead()) the first time this project's full
 * app_init()/app_loop() flow ran ENET init immediately after
 * APP_ENABLE_STORAGE's flash mount/read/write, a combination never
 * exercised before. This define doesn't necessarily explain WHY that
 * specific MDIO transaction stalled, but it's a real, independent
 * robustness gap regardless -- this port's whole architecture is built
 * on "non-blocking, no infinite waits" (see below), and this one vendor
 * SDK function was silently violating that via a missing compiler flag,
 * same class of landmine as the RMII50M_MODE one above.
 * ***************************************************************
 *
 * Unlike most bring-up in this port, ENET/PHY/lwIP init is NON-BLOCKING
 * here (the standalone test it's adapted from used a blocking
 * wait-for-linkup loop matching Embedded Artists' own reference demo,
 * which is fine for a one-shot test but not for the real app -- CLAUDE.md's
 * "non-blocking state machines, not blocking loops with timeouts" rule
 * applies). netif_add() already starts lwIP's own periodic PHY
 * link-status poll internally (ethernetif.c's probe_link_cyclic(),
 * driven by sys_check_timeouts()) -- link-up and DHCP binding both
 * happen asynchronously over subsequent enet_lwip_rt1062_poll() calls,
 * with no need to block app_init() waiting for a cable to be plugged in.
 */

#ifndef ENET_LWIP_RT1062_H
#define ENET_LWIP_RT1062_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Call once from app_init(), BEFORE any tcp_lwip_*_open() call (those
 * need a netif to already exist). Brings up ENET pins/clock/PHY/MDIO,
 * starts lwIP (lwip_init(), netif_add(), netif_set_up()), and starts
 * DHCP -- all non-blocking; returns immediately without waiting for
 * link-up or a DHCP lease.
 *
 * NOTE 2026-07-16: briefly changed to NOT start DHCP itself, requiring
 * a separate mandatory enet_lwip_rt1062_apply_network_settings() call
 * right after (a real fix for app->settings.use_dhcp/rabbit_ip/
 * rabbit_gateway being ignored at boot) -- REVERTED back to this
 * unconditional-dhcp_start() behavior the same day, per explicit
 * instruction, after that change's app_init.c call site went live
 * alongside APP_ENABLE_STORAGE and app_init() started hanging
 * completely (traced to inside THIS function's own netif_add()/
 * PHY_Init(), correlated with STORAGE's flash mount/read/write running
 * immediately before it -- see project memory,
 * project_gps_spi_bringup.md). Root cause not yet confirmed to be
 * either change specifically; reverted to restore a known-working
 * device first. enet_lwip_rt1062_apply_network_settings() itself is
 * unchanged and still available -- see its own doc comment -- just no
 * longer called from here.
 *
 * out_mac_address (may be NULL if you don't need it elsewhhere): filled
 * with the real board MAC address (I2C EEPROM, see mac_addr_rt1062.c),
 * the same address assigned to the netif -- copy into app->mac_address
 * for the UDP discovery responder / GPRS batch sender, which use it
 * independently of the netif itself. Left UNTOUCHED if this call fails
 * (see return value below).
 *
 * RETURN VALUE, added 2026-07-16: 0 on success, -1 if the PHY never
 * responded after a bounded number of reset-and-retry attempts (see
 * enet_phy_preflight_check() in the .c file) -- a real, recoverable
 * failure mode discovered on this exact board: a debug probe attached
 * via SWD/JTAG can interfere with GPIO1 pins 9/10 (ENET_RST/ENET_INT,
 * dual-purpose with JTAG_TDI/TDO) during the PHY reset sequence,
 * causing a single MDIO transaction to fail. Previously this took the
 * ENTIRE BOARD down (PHY_Init()'s own internal retry doesn't cover a
 * genuine MDIO transfer failure, only a successful-but-wrong-ID read,
 * so one bad transaction fell straight through to lwIP's
 * LWIP_ASSERT -> for(;;){} halt), requiring a full power-cycle with
 * the probe physically unplugged to recover -- a real workflow cost
 * during iterative debugging. On -1, ENET/lwIP/TCP are left entirely
 * uninitialized -- the caller (app_init.c) must skip every
 * tcp_lwip_*_open() call and continue booting everything else (display,
 * GPS, BMS, etc) rather than treating this as fatal to the whole boot.
 * This is new resilience logic, not something the original Dynamic C
 * source has an equivalent of (it never shared pins with a debug
 * probe the way this board does) -- added per explicit request to stop
 * needing the probe physically unplugged for every single test cycle.
 */
int enet_lwip_rt1062_init(uint8_t out_mac_address[6]);

/*
 * Call once per main-loop iteration, unconditionally, alongside (before
 * or after, order doesn't matter) tcp_lwip_poll() -- this is the
 * "board's ENET driver" half that tcp_transport_lwip.h's tcp_lwip_poll()
 * doc comment says must be pumped separately. Drives the actual
 * Ethernet RX path (ethernetif_input()); tcp_lwip_poll() drives lwIP's
 * timers (sys_check_timeouts()) -- both are needed every iteration.
 */
void enet_lwip_rt1062_poll(void);

/* True once the PHY has reported link-up (netif_is_link_up(), driven
 * by ethernetif.c's probe_link_cyclic()) -- distinct from, and
 * necessarily earlier than, DHCP getting a lease. Added 2026-07-16 to
 * distinguish "no cable/PHY link" from "link's fine, but no DHCP
 * server/lease" during bring-up -- see app_loop.c's diagnostic trace. */
bool enet_lwip_rt1062_is_link_up(void);

/* True once DHCP has bound an address -- not required before opening
 * TCP listeners (tcp_bind()/tcp_listen() work fine pre-DHCP; clients
 * just can't reach the board yet), but useful if you want to gate
 * anything else (status display, discovery broadcasts) on having a
 * real IP first. */
bool enet_lwip_rt1062_has_ip(void);

/*
 * Copies the netif's current IPv4 address into out_ip[4] (network byte
 * order, i.e. out_ip[0] is the first dotted-quad octet). Valid whether
 * the address came from DHCP or a static assignment -- before any
 * address is bound this reads back 0.0.0.0, same as an unconfigured
 * lwIP netif. Was `DynamicIP[]`, the original's own copy of the
 * DHCP-assigned address used for touchscreen display only.
 */
void enet_lwip_rt1062_get_ip(uint8_t out_ip[4]);

/*
 * Same idea as enet_lwip_rt1062_get_ip() above, for the netif's current
 * gateway address instead. Added 2026-07-20 -- was `di->dhcp_server`,
 * copied into `Settings.RabbitGateway[]` inside the original's
 * `updateDIPA()` DHCP-success callback (ACTIVERFID_V1.02_UHF.c line
 * ~471) -- this port had no equivalent anywhere, so
 * app->settings.rabbit_gateway never reflected a real DHCP-assigned
 * gateway, only ever the static-IP default/whatever was last typed via
 * the keypad. See app_loop.c's trace_dhcp_lease_once() for the actual
 * update call site (same one-shot "lease just landed" moment the
 * original's callback fires at).
 */
void enet_lwip_rt1062_get_gateway(uint8_t out_gw[4]);

/*
 * Was UpdateRabbitIP()'s IFS_DOWN + reconfigure + IFS_UP sequence --
 * call when the touchscreen's DHCP toggle or static IP/gateway fields
 * change (see app_genie_dispatch.c's GENIE_DHCP handler and the
 * GENIE_BUTTON_SETLAN keypad-entry handler). Brings the interface
 * down, reconfigures it, brings it back up:
 *
 * NOTE 2026-07-16: was briefly ALSO called from app_init() right after
 * enet_lwip_rt1062_init() (a real SetRabbitIP()-equivalent fix, so a
 * saved static-IP config would actually apply at boot, not just when
 * touched live) -- that boot-time call site was REVERTED per explicit
 * instruction after app_init() started hanging elsewhere the same
 * session (see enet_lwip_rt1062_init()'s own updated comment). This
 * function itself is UNCHANGED and still correct for the touchscreen
 * path; only the app_init.c boot-time call site was removed. The
 * underlying gap this was fixing (saved static-IP settings being
 * ignored at boot) is real and still open -- revisit once the hang is
 * understood, ideally re-adding this call in isolation rather than
 * bundled with re-enabling APP_ENABLE_STORAGE at the same time.
 *
 *  - use_dhcp!=0: restarts DHCP and sets ip[] + a hardcoded 255.255.0.0
 *    netmask as the DHCP-fallback static config -- matches the
 *    original's own "Specify fallbacks for DHCP" ifconfig call
 *    (IFS_IPADDR/IFS_NETMASK set right after starting DHCP). gateway[]
 *    is ignored in this branch, matching the original (DHCP supplies
 *    its own router). See enet_lwip_rt1062_poll_dhcp_fallback() for
 *    the actual fallback-on-timeout behavior -- lwIP's dhcp_start()
 *    has no built-in timeout/fallback of its own, unlike Rabbit's
 *    ifconfig(IFS_DHCP_TIMEOUT/IFS_DHCP_FALLBACK), so this port tracks
 *    it explicitly.
 *  - use_dhcp==0: stops DHCP, applies ip[]/gateway[] as a static
 *    config, same hardcoded netmask.
 *
 * Netmask 255.255.0.0 is CONFIRMED from the original's own literal
 * `IFS_NETMASK, 0xFFFF0000uL` in BOTH branches of UpdateRabbitIP() --
 * not this port's guess or a default.
 *
 * NOT applied here: DNS server (Settings.RabbitDNS / app->settings.
 * rabbit_dns). LWIP_DNS is off in this project's lwipopts.h, and
 * nothing in this port resolves hostnames yet -- every connection
 * target is already a raw IP (see ip_addr_parse.h's own doc comment
 * on that separate, not-yet-built gap). Flagged, not silently dropped.
 */
void enet_lwip_rt1062_apply_network_settings(int use_dhcp, const uint8_t ip[4], const uint8_t gateway[4]);

/*
 * Call once per main-loop iteration, alongside enet_lwip_rt1062_poll()
 * (order doesn't matter). No-op unless enet_lwip_rt1062_apply_network_settings()
 * was just called with use_dhcp!=0 and DHCP hasn't bound an address
 * yet -- once 8 real-world seconds have elapsed with no lease, falls
 * back to the same static ip[] that call was given. 8 seconds is
 * CONFIRMED from the original's own `IFS_DHCP_TIMEOUT, 8`.
 */
void enet_lwip_rt1062_poll_dhcp_fallback(void);

#ifdef __cplusplus
}
#endif

#endif /* ENET_LWIP_RT1062_H */
