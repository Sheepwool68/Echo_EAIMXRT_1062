/*
 * enet_pins_rt1062.c
 *
 * See enet_pins_rt1062.h -- deliberately kept outside Config
 * Tools-generated board/pin_mux.c. Pin assignments and PAD_CTL values
 * are CONFIRMED against Embedded Artists' own SDK reference
 * (boards/evkcmimxrt1060/lwip_examples/lwip_dhcp/bm/pin_mux.c). Unlike
 * most files in this scaffold, this one HAS been compiled and tested on
 * real hardware (in the separate real MCUXpresso project this port
 * developed alongside, not this scaffold repo directly) -- link-up +
 * DHCP + TCP all confirmed working 2026-07-14. See
 * enet_lwip_rt1062.h for one critical, easy-to-miss integration
 * requirement (a compiler define) this depends on. RMII
 * pinout (RX_DATA00/01, RX_EN, TX_DATA00/01, TX_EN, REF_CLK, RX_ER,
 * MDC, MDIO) plus PHY reset (GPIO1 pin 9) / PHY interrupt (GPIO1 pin
 * 10), both muxed here as plain GPIO (driven/read in the ENET/PHY init
 * code, not here -- this file only does electrical pad configuration).
 *
 * PAD_CTL values translated from that reference's IOMUX_* bit-flag
 * macros into raw hex, using the SAME register bit layout already
 * confirmed earlier this session (pad control register: DSE[5:3],
 * SPEED[7:6], PKE[12], PUE[13], PUS[15:14], HYS[16]):
 *   - RX_DATA00/01, RX_EN, TX_DATA00/01, TX_EN, RX_ER, MDC:
 *     PULL_UP100K | SPD_MAX | DSE(5) | SLEW_FAST = 0xB0E9
 *   - REF_CLK (ALT1, SION on): PULL_NONE | SPD_LOW | DSE(6) | SLEW_FAST = 0x0031
 *   - MDIO (open-drain, bidirectional): PULL_UP100K | ODE | SPD_LOW | DSE(5) | SLEW_FAST = 0xB829
 */

#include "fsl_common.h"
#include "fsl_iomuxc.h"
#include "fsl_clock.h"
#include "enet_pins_rt1062.h"

void BOARD_InitENETPins(void)
{
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_04_ENET_RX_DATA00, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_05_ENET_RX_DATA01, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_06_ENET_RX_EN, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_07_ENET_TX_DATA00, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_08_ENET_TX_DATA01, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_09_ENET_TX_EN, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_10_ENET_REF_CLK, 1U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_B1_11_ENET_RX_ER, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_EMC_40_ENET_MDC, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_EMC_41_ENET_MDIO, 0U);

    /* PHY reset / interrupt pins, muxed as plain GPIO -- driven/read in
     * the ENET/PHY init code (BOARD_ENET_PHY_RESET-equivalent), not
     * here. */
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_09_GPIO1_IO09, 0U);
    IOMUXC_SetPinMux(IOMUXC_GPIO_AD_B0_10_GPIO1_IO10, 0U);

    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_04_ENET_RX_DATA00, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_05_ENET_RX_DATA01, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_06_ENET_RX_EN, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_07_ENET_TX_DATA00, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_08_ENET_TX_DATA01, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_09_ENET_TX_EN, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_10_ENET_REF_CLK, 0x0031U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_11_ENET_RX_ER, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_EMC_40_ENET_MDC, 0xB0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_EMC_41_ENET_MDIO, 0xB829U);
}

void BOARD_InitENETClock(void)
{
    /* CONFIRMED from Embedded Artists' own reference (BOARD_InitModuleClock()
     * in their lwip_dhcp/bm example) -- same config values, same call. */
    const clock_enet_pll_config_t config = {
        .enableClkOutput = true,
        .enableClkOutput25M = false,
        .loopDivider = 1
    };
    CLOCK_InitEnetPll(&config);
}
