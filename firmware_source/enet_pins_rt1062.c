/*
 * enet_pins_rt1062.c
 *
 * See enet_pins_rt1062.h -- deliberately kept outside Config
 * Tools-generated board/pin_mux.c. Pin assignments and PAD_CTL values
 * are CONFIRMED against Embedded Artists' own SDK reference
 * (boards/evkcmimxrt1060/lwip_examples/lwip_dhcp/bm/pin_mux.c), which
 * the user has already run successfully on this exact board -- RMII
 * pinout (RX_DATA00/01, RX_EN, TX_DATA00/01, TX_EN, REF_CLK, RX_ER,
 * MDC, MDIO) plus PHY reset (GPIO1 pin 9) / PHY interrupt (GPIO1 pin
 * 10), both muxed here as plain GPIO (driven/read in the ENET/PHY init
 * code, not here -- this file only does electrical pad configuration).
 *
 * PAD_CTL bit layout (pad control register: SRE[0], DSE[5:3],
 * SPEED[7:6], ODE[11], PKE[12], PUE[13], PUS[15:14], HYS[16]) --
 * independently cross-checked 2026-07-21 against this project's own
 * Config-Tools-generated, hardware-confirmed SPI pin values
 * (board/pin_mux.c's 0x1079/0x0100B0 for SPI_SDO/SPI_SDI), not just
 * asserted.
 *
 * REVISED 2026-07-21 -- found and fixed a real inconsistency in the
 * original values (still shown as history below): decoding REF_CLK's
 * own 0x0031 gives DSE=3/7 and SPEED=lowest grade, even though this
 * file's own original comment claimed "DSE(6)" for it -- the comment
 * didn't match its own encoded value, meaning it was never actually
 * verified bit-by-bit, just asserted. REF_CLK is the one 50MHz clock
 * the RT1062 itself generates and drives OUT to the PHY
 * (kIOMUXC_GPR_ENET1TxClkOutputDir set true in enet_lwip_rt1062.c) --
 * the single most timing-critical signal on the whole interface, yet
 * it had the WEAKEST pad config of anything here. Diagnostic
 * A/B-testing (2026-07-21, see project memory) proved a "bad checksum,
 * no comms" symptom on outgoing TCP data reproduces even in a minimal,
 * isolated test project with none of this port's own code involved --
 * pointing at exactly this class of electrical-layer cause, the same
 * one already found and fixed twice before on this board (GPS SPI pad
 * drive-strength, button LED pin mux) via the identical "silicon
 * defaults/hand-typed value are inadequate for a real signal on this
 * board" mechanism.
 *
 * Values below, ALL independently bit-decoded (not copied from any
 * external reference) to SRE=1(fast)/DSE=7(max)/SPEED=3(max) for every
 * actively-driven RMII output signal, plus HYS=1 added on inputs:
 *   - TX_DATA00/01, TX_EN, REF_CLK (RT1062-driven outputs):
 *     SLEW_FAST | DSE_MAX(7) | SPEED_MAX(3) | no pull (actively driven,
 *     none needed) = 0x00F9
 *   - RX_DATA00/01, RX_EN, RX_ER (PHY-driven inputs -- DSE/SPEED don't
 *     apply to an input, only SRE/DSE apply to the pin's OWN output
 *     driver): original 0xB0E9 (PULL_UP100K | SPD_MAX | DSE(5) |
 *     SLEW_FAST) plus HYS(1) enabled for a cleaner digital threshold
 *     = 0x1B0E9
 *   - MDC, MDIO: UNCHANGED (0xB0E9 / 0xB829) -- MDIO reads have been
 *     reliable throughout this entire project; no evidence either
 *     needs touching, so left alone rather than changed on spec.
 *
 * Original (now superseded) values, kept here for the record:
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

    /* Inputs (PHY drives these) -- original PULL_UP100K|SPD_MAX|DSE(5)|
     * SLEW_FAST plus hysteresis added, see this file's header comment. */
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_04_ENET_RX_DATA00, 0x1B0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_05_ENET_RX_DATA01, 0x1B0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_06_ENET_RX_EN, 0x1B0E9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_11_ENET_RX_ER, 0x1B0E9U);

    /* Outputs (RT1062 drives these to the PHY) -- maxed to DSE(7)/
     * SPEED_MAX(3)/SLEW_FAST, no pull needed on an actively-driven
     * push-pull line. REF_CLK is the fix for the weak/low-speed
     * 0x0031 it had before -- see this file's header comment. */
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_07_ENET_TX_DATA00, 0x00F9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_08_ENET_TX_DATA01, 0x00F9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_09_ENET_TX_EN, 0x00F9U);
    IOMUXC_SetPinConfig(IOMUXC_GPIO_B1_10_ENET_REF_CLK, 0x00F9U);

    /* UNCHANGED -- MDC/MDIO have been reliable throughout this entire
     * project, no evidence either needs touching. */
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
