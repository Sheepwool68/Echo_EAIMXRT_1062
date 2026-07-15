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
 * out_mac_address (may be NULL if you don't need it elsewhhere): filled
 * with the real board MAC address (I2C EEPROM, see mac_addr_rt1062.c),
 * the same address assigned to the netif -- copy into app->mac_address
 * for the UDP discovery responder / GPRS batch sender, which use it
 * independently of the netif itself.
 */
void enet_lwip_rt1062_init(uint8_t out_mac_address[6]);

/*
 * Call once per main-loop iteration, unconditionally, alongside (before
 * or after, order doesn't matter) tcp_lwip_poll() -- this is the
 * "board's ENET driver" half that tcp_transport_lwip.h's tcp_lwip_poll()
 * doc comment says must be pumped separately. Drives the actual
 * Ethernet RX path (ethernetif_input()); tcp_lwip_poll() drives lwIP's
 * timers (sys_check_timeouts()) -- both are needed every iteration.
 */
void enet_lwip_rt1062_poll(void);

/* True once DHCP has bound an address -- not required before opening
 * TCP listeners (tcp_bind()/tcp_listen() work fine pre-DHCP; clients
 * just can't reach the board yet), but useful if you want to gate
 * anything else (status display, discovery broadcasts) on having a
 * real IP first. */
bool enet_lwip_rt1062_has_ip(void);

#ifdef __cplusplus
}
#endif

#endif /* ENET_LWIP_RT1062_H */
