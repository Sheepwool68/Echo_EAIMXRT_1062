/*
 * enet_lwip_rt1062.c
 *
 * See enet_lwip_rt1062.h -- adapted from a standalone bring-up test
 * confirmed working on real hardware 2026-07-14 (link-up, DHCP, and a
 * TCP echo server all verified), itself blueprinted from Embedded
 * Artists' own confirmed-working SDK reference example
 * (boards/evkcmimxrt1060/lwip_examples/lwip_dhcp/bm). Structurally the
 * same PHY reset/MDIO/pin/clock sequence as that test; the difference
 * here is init is non-blocking (no wait-for-linkup loop), matching this
 * port's own architecture rather than the reference demo's simpler
 * one-shot style.
 *
 * ==========================================================================
 * SCAFFOLD tier note: this specific file's LOGIC has been compiled and
 * run successfully on real hardware (in the separate real MCUXpresso
 * project this port developed alongside) -- but as integrated into
 * THIS repo's app_init.c/app_loop.c, it has not been rebuilt/retested
 * here. Review the call sites in app_init.c/app_loop.c against the
 * working standalone version if anything behaves unexpectedly.
 * ==========================================================================
 */

#include "enet_lwip_rt1062.h"
#include "enet_pins_rt1062.h"
#include "mac_addr_rt1062.h"

#include "fsl_gpio.h"
#include "fsl_common.h"
#include "fsl_enet.h"
#include "fsl_phy.h"
#include "fsl_phyksz8081.h"
#include "fsl_iomuxc.h"

#include "lwip/init.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"
#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"
#include "ethernetif.h"

#include "systick_ms_rt1062.h"
#include "ms_time.h"
#include <string.h>

/* Redirects PRINTF to LPUART5 instead of semihosting -- see
 * debug_console_rt1062.h's own header comment. Needed here for the
 * PHY-not-responding warning below (the boot-progress checkpoints this
 * was originally added alongside, 2026-07-16, have since been removed
 * now that the ENET hang they were bracketing is long resolved -- see
 * project memory/CLAUDE.md). */
#include "debug_console_rt1062.h"
#undef PRINTF
#define PRINTF debug_printf

/* CONFIRMED from Embedded Artists' own board.h (BOARD_ENET0_PHY_ADDRESS) --
 * KSZ8081 PHY at MDIO address 0x02. */
#define ENET_PHY_ADDRESS 0x02U
#define ENET_INSTANCE    ENET

static phy_ksz8081_resource_t s_phy_resource;
static phy_handle_t s_phy_handle;
static struct netif s_netif;

/* CONFIRMED from the original's own `IFS_DHCP_TIMEOUT, 8` in
 * UpdateRabbitIP() -- not a guess. */
#define DHCP_FALLBACK_TIMEOUT_MS 8000u

static uint8_t s_dhcp_fallback_ip[4];
static uint32_t s_dhcp_fallback_started_ms;
static int s_dhcp_fallback_pending;

/* -------------------------------------------------------------------
 * PHY reset + MDIO glue -- identical to the confirmed-working
 * standalone test.
 * ---------------------------------------------------------------- */

/* CONFIRMED from Embedded Artists' board.h's BOARD_ENET_PHY_RESET macro
 * and the User's Guide's own pin documentation: GPIO1 pin 9 = ENET_RST
 * (active low), GPIO1 pin 10 = ENET_INT (pulled up before reset) --
 * these are also JTAG_TDI/TDO, per EA's own dual-purpose pin design.
 * Pin muxing to plain GPIO is done in enet_pins_rt1062.c; this just
 * drives the actual reset sequence. */
static void enet_phy_hw_reset(void)
{
    gpio_pin_config_t cfg = {kGPIO_DigitalOutput, 0, kGPIO_NoIntmode};

    GPIO_PinInit(GPIO1, 9U, &cfg);
    GPIO_PinInit(GPIO1, 10U, &cfg);

    GPIO_PinWrite(GPIO1, 10U, 1u); /* pull up ENET_INT before reset */
    GPIO_PinWrite(GPIO1, 9U, 0u);  /* assert reset */
    SDK_DelayAtLeastUs(10000U, CLOCK_GetFreq(kCLOCK_CpuClk));
    GPIO_PinWrite(GPIO1, 9U, 1u);  /* release reset */
    SDK_DelayAtLeastUs(100U, CLOCK_GetFreq(kCLOCK_CpuClk));
}

static void mdio_init(void)
{
    (void)CLOCK_EnableClock(s_enetClock[ENET_GetInstance(ENET_INSTANCE)]);
    ENET_SetSMI(ENET_INSTANCE, CLOCK_GetFreq(kCLOCK_IpgClk), false);
}

static status_t mdio_write(uint8_t phyAddr, uint8_t regAddr, uint16_t data)
{
    return ENET_MDIOWrite(ENET_INSTANCE, phyAddr, regAddr, data);
}

static status_t mdio_read(uint8_t phyAddr, uint8_t regAddr, uint16_t *pData)
{
    return ENET_MDIORead(ENET_INSTANCE, phyAddr, regAddr, pData);
}

/* CONFIRMED from fsl_phyksz8081.c's own PHY_CONTROL_ID1 (0x22U, its
 * expected KSZ8081 PHY_ID1_REG reply) -- not exported via a header, so
 * duplicated here rather than modifying vendor SDK source. */
#define ENET_PHY_EXPECTED_ID1 0x22U
#define ENET_PHY_PREFLIGHT_ATTEMPTS 5U

/* Bounded pre-flight PHY-presence check, added 2026-07-16 -- NOT from
 * the original (Dynamic C never had this exact failure mode; nothing
 * in the Rabbit design shared pins with a debug probe the way GPIO1
 * pins 9/10 do here, see this file's header comment on the JTAG_TDI/
 * TDO overlap). PHY_KSZ8081_Init() (called deep inside netif_add() via
 * ethernetif_phy_init()) does NOT retry on a genuine MDIO transfer
 * failure -- its own internal retry loop only covers "read succeeded
 * but ID didn't match yet," not "the read itself timed out." A single
 * transient MDIO glitch (e.g. debug-probe interference on the
 * JTAG-shared reset/interrupt pins during enet_phy_hw_reset()) fails
 * PHY_Init() immediately, and lwIP turns that into an unrecoverable
 * LWIP_ASSERT -> for(;;){} halt in sys_arch.c -- taking the whole
 * board down, not just networking, every time. This gives the PHY a
 * bounded number of fresh-reset-and-retry chances to come up cleanly
 * BEFORE ever calling netif_add(), so a transient glitch doesn't
 * require a full board power-cycle (with the debug probe physically
 * unplugged) to recover from. If the PHY still isn't responding after
 * this, ENET is skipped for this boot (see enet_lwip_rt1062_init()'s
 * return value) rather than hanging forever -- the rest of app_init()
 * (display, GPS, BMS, etc) still gets to run and be tested. */
static bool enet_phy_preflight_check(void)
{
    uint32_t attempt;

    for (attempt = 0; attempt < ENET_PHY_PREFLIGHT_ATTEMPTS; attempt++) {
        uint16_t id1 = 0;
        status_t result;

        enet_phy_hw_reset();
        mdio_init();
        result = mdio_read(ENET_PHY_ADDRESS, PHY_ID1_REG, &id1);
        if (result == kStatus_Success && id1 == ENET_PHY_EXPECTED_ID1) {
            return true;
        }
    }
    return false;
}

/* -------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------- */

/* Returns 0 on success, -1 if the PHY never responded after
 * ENET_PHY_PREFLIGHT_ATTEMPTS resets (see enet_phy_preflight_check()) --
 * in that case ENET/lwIP/TCP are left entirely uninitialized for this
 * boot; the caller (app_init.c) must skip any tcp_lwip_*_open() calls
 * and continue booting everything else rather than treating this as a
 * fatal error. */
int enet_lwip_rt1062_init(uint8_t out_mac_address[6])
{
    ethernetif_config_t enet_config = {
        .phyHandle = &s_phy_handle,
        .phyAddr = ENET_PHY_ADDRESS,
        .phyOps = &phyksz8081_ops,
        .phyResource = &s_phy_resource,
    };

    BOARD_InitENETPins();
    BOARD_InitENETClock();

    /* CONFIRMED required, from Embedded Artists' own reference main() --
     * sets the RT1062 to DRIVE the RMII 50MHz reference clock to the
     * PHY, rather than leaving that pin in its default input mode. */
    IOMUXC_EnableMode(IOMUXC_GPR, kIOMUXC_GPR_ENET1TxClkOutputDir, true);

    s_phy_resource.read = mdio_read;
    s_phy_resource.write = mdio_write;

    /* Real, pre-programmed board MAC address (I2C EEPROM) -- see
     * mac_addr_rt1062.c. Also handed back to the caller so app_init()
     * can populate app->mac_address for the UDP discovery responder /
     * GPRS batch sender, which use it independently of the netif.
     * MOVED 2026-07-20 -- was sequenced AFTER enet_phy_preflight_check()
     * below, so a failed PHY (a real, already-documented failure mode on
     * this board -- debug-probe/JTAG interference with the reset pins,
     * see project memory) hit the early return before this ever ran,
     * leaving app->mac_address permanently zeroed for that boot. This
     * I2C EEPROM read is on a completely separate bus from the Ethernet
     * PHY -- it has no dependency on PHY/link state at all, so it
     * shouldn't be gated behind PHY success. The MAC is a fixed hardware
     * property (not user-changeable, matches the original's own
     * `pd_getaddress(IF_ETH0, MacAddress)` -- queries the interface's
     * configured address, not something link-state-dependent), so it
     * should be available for display/discovery/GPRS-batch use even on
     * a boot where ENET/TCP itself doesn't come up. */
    MAC_Read(enet_config.macAddress);
    if (out_mac_address != NULL) {
        out_mac_address[0] = enet_config.macAddress[0];
        out_mac_address[1] = enet_config.macAddress[1];
        out_mac_address[2] = enet_config.macAddress[2];
        out_mac_address[3] = enet_config.macAddress[3];
        out_mac_address[4] = enet_config.macAddress[4];
        out_mac_address[5] = enet_config.macAddress[5];
    }

    if (!enet_phy_preflight_check()) {
        PRINTF("enet_init: PHY not responding after %u reset attempts -- "
               "skipping ENET/TCP this boot\r\n",
               (unsigned)ENET_PHY_PREFLIGHT_ATTEMPTS);
        return -1;
    }

    enet_config.srcClockHz = CLOCK_GetFreq(kCLOCK_IpgClk);

    lwip_init();

    netif_add(&s_netif, NULL, NULL, NULL, &enet_config, ethernetif0_init, ethernet_input);
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);

    /* NON-BLOCKING, unlike the standalone bring-up test this is adapted
     * from (which used ethernetif_wait_linkup() in a blocking retry
     * loop, matching Embedded Artists' own reference demo). netif_add()
     * above already started lwIP's own periodic PHY link-status poll
     * internally (ethernetif.c's probe_link_cyclic(), driven by
     * sys_check_timeouts() -- see enet_lwip_rt1062_poll()/tcp_lwip_poll()),
     * so link-up will be detected asynchronously whenever it actually
     * happens, with no need to block app_init() here waiting for a
     * cable. dhcp_start() is likewise non-blocking by lwIP's own
     * design -- negotiation proceeds via the same timer machinery.
     *
     * RESTORED unconditional dhcp_start() here 2026-07-16 (had been
     * removed the same day in favor of a SetRabbitIP()-equivalent
     * boot-time call to enet_lwip_rt1062_apply_network_settings(),
     * which correctly fixed a real gap -- app->settings.use_dhcp/
     * rabbit_ip/rabbit_gateway being silently ignored at boot -- but
     * that whole change was never confirmed working on real hardware
     * before app_init() started hanging elsewhere (traced to inside
     * netif_add()/PHY_Init(), immediately below, correlated with
     * APP_ENABLE_STORAGE running flash mount/read/write right before
     * this function for the first time -- see project memory). Per
     * explicit instruction ("go back to what worked"), reverted this
     * specific function back to its own last hardware-confirmed
     * behavior while STORAGE stays off and the real hang cause gets
     * investigated separately. enet_lwip_rt1062_apply_network_settings()
     * itself is UNCHANGED and still exists/still used by the
     * touchscreen's live DHCP-toggle path -- only the NEW boot-time
     * call site in app_init.c was reverted, not the function itself. */
    dhcp_start(&s_netif);
    return 0;
}

void enet_lwip_rt1062_poll(void)
{
    ethernetif_input(&s_netif);
}

bool enet_lwip_rt1062_is_link_up(void)
{
    return netif_is_link_up(&s_netif);
}

bool enet_lwip_rt1062_has_ip(void)
{
    return dhcp_supplied_address(&s_netif);
}

void enet_lwip_rt1062_get_ip(uint8_t out_ip[4])
{
    const ip4_addr_t *addr = netif_ip4_addr(&s_netif);
    out_ip[0] = ip4_addr1(addr);
    out_ip[1] = ip4_addr2(addr);
    out_ip[2] = ip4_addr3(addr);
    out_ip[3] = ip4_addr4(addr);
}

void enet_lwip_rt1062_get_gateway(uint8_t out_gw[4])
{
    const ip4_addr_t *gw = netif_ip4_gw(&s_netif);
    out_gw[0] = ip4_addr1(gw);
    out_gw[1] = ip4_addr2(gw);
    out_gw[2] = ip4_addr3(gw);
    out_gw[3] = ip4_addr4(gw);
}

void enet_lwip_rt1062_apply_network_settings(int use_dhcp, const uint8_t ip[4], const uint8_t gateway[4])
{
    ip4_addr_t addr, netmask, gw;

    netif_set_down(&s_netif);

    /* CONFIRMED 255.255.0.0 in both branches -- see this function's
     * header comment. */
    IP4_ADDR(&netmask, 255, 255, 0, 0);

    /* dhcp_release_and_stop() first in both branches -- was IFS_DHCP,0/
     * an implicit fresh DHCP restart in the original; lwIP's dhcp_start()
     * doesn't itself guarantee a clean restart if a previous negotiation
     * (or lease) is still active, so stop explicitly before either
     * reconfiguring statically or starting DHCP again. */
    dhcp_release_and_stop(&s_netif);

    if (use_dhcp) {
        /* Was the "Specify fallbacks for DHCP" ifconfig call
         * (IFS_IPADDR/IFS_NETMASK set right after IFS_DHCP,1) -- a
         * static config lwIP will keep using until DHCP actually
         * supplies a lease, and this port's own fallback-timeout
         * (enet_lwip_rt1062_poll_dhcp_fallback()) re-applies if DHCP
         * never completes. No gateway set here, matching the
         * original -- DHCP supplies its own router once bound. */
        IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);
        IP4_ADDR(&gw, 0, 0, 0, 0);
        netif_set_addr(&s_netif, &addr, &netmask, &gw);

        memcpy(s_dhcp_fallback_ip, ip, 4);
        s_dhcp_fallback_started_ms = systick_ms_now();
        s_dhcp_fallback_pending = 1;

        netif_set_up(&s_netif);
        dhcp_start(&s_netif);
    } else {
        s_dhcp_fallback_pending = 0;

        IP4_ADDR(&addr, ip[0], ip[1], ip[2], ip[3]);
        IP4_ADDR(&gw, gateway[0], gateway[1], gateway[2], gateway[3]);
        netif_set_addr(&s_netif, &addr, &netmask, &gw);

        netif_set_up(&s_netif);
    }
}

void enet_lwip_rt1062_poll_dhcp_fallback(void)
{
    if (!s_dhcp_fallback_pending) {
        return;
    }
    if (dhcp_supplied_address(&s_netif)) {
        s_dhcp_fallback_pending = 0;
        return;
    }
    if (ms_has_elapsed(systick_ms_now(), s_dhcp_fallback_started_ms, DHCP_FALLBACK_TIMEOUT_MS)) {
        ip4_addr_t addr, netmask, gw;

        IP4_ADDR(&netmask, 255, 255, 0, 0);
        IP4_ADDR(&addr, s_dhcp_fallback_ip[0], s_dhcp_fallback_ip[1],
                  s_dhcp_fallback_ip[2], s_dhcp_fallback_ip[3]);
        IP4_ADDR(&gw, 0, 0, 0, 0);

        dhcp_release_and_stop(&s_netif);
        netif_set_addr(&s_netif, &addr, &netmask, &gw);
        s_dhcp_fallback_pending = 0;
    }
}
