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
#include "netif/ethernet.h"
#include "ethernetif.h"

/* CONFIRMED from Embedded Artists' own board.h (BOARD_ENET0_PHY_ADDRESS) --
 * KSZ8081 PHY at MDIO address 0x02. */
#define ENET_PHY_ADDRESS 0x02U
#define ENET_INSTANCE    ENET

static phy_ksz8081_resource_t s_phy_resource;
static phy_handle_t s_phy_handle;
static struct netif s_netif;

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

/* -------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------- */

void enet_lwip_rt1062_init(uint8_t out_mac_address[6])
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

    enet_phy_hw_reset();

    mdio_init();
    s_phy_resource.read = mdio_read;
    s_phy_resource.write = mdio_write;

    /* Real, pre-programmed board MAC address (I2C EEPROM) -- see
     * mac_addr_rt1062.c. Also handed back to the caller so app_init()
     * can populate app->mac_address for the UDP discovery responder /
     * GPRS batch sender, which use it independently of the netif. */
    MAC_Read(enet_config.macAddress);
    if (out_mac_address != NULL) {
        out_mac_address[0] = enet_config.macAddress[0];
        out_mac_address[1] = enet_config.macAddress[1];
        out_mac_address[2] = enet_config.macAddress[2];
        out_mac_address[3] = enet_config.macAddress[3];
        out_mac_address[4] = enet_config.macAddress[4];
        out_mac_address[5] = enet_config.macAddress[5];
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
     * design -- negotiation proceeds via the same timer machinery. */
    dhcp_start(&s_netif);
}

void enet_lwip_rt1062_poll(void)
{
    ethernetif_input(&s_netif);
}

bool enet_lwip_rt1062_has_ip(void)
{
    return dhcp_supplied_address(&s_netif);
}
