/*
 * enet_pins_rt1062.h
 *
 * ENET (RMII) pin muxing, kept in a hand-written file OUTSIDE
 * board/pin_mux.c deliberately -- pin_mux.c is MCUXpresso Config
 * Tools-generated and gets silently overwritten if the Pins tool is
 * ever opened and saved again (a known risk in this project, see
 * project memory on regenerated files). Values here are CONFIRMED
 * against Embedded Artists' own SDK reference (lwip_dhcp/bm example),
 * which the user has already run successfully on this exact board.
 */

#ifndef ENET_PINS_RT1062_H
#define ENET_PINS_RT1062_H

#ifdef __cplusplus
extern "C" {
#endif

/* Call once at startup, before ENET0_init()/PHY init. Configures the
 * RMII pin set (RX_DATA00/01, RX_EN, TX_DATA00/01, TX_EN, REF_CLK,
 * RX_ER, MDC, MDIO) plus the PHY reset (GPIO1 pin 9) and PHY interrupt
 * (GPIO1 pin 10) pins as plain GPIO. */
void BOARD_InitENETPins(void);

/* Call once at startup (order doesn't matter relative to
 * BOARD_InitENETPins(), but must run before ENET_Init()/MDIO use).
 * Starts the ENET PLL that provides the RMII reference clock -- was
 * BOARD_InitModuleClock() in the EA reference; CLOCK_InitEnetPll()
 * itself is a standard SDK function (fsl_clock.h), already available
 * in this project, just never called until now. */
void BOARD_InitENETClock(void);

#ifdef __cplusplus
}
#endif

#endif /* ENET_PINS_RT1062_H */
