/*
 * bms_init.c
 *
 * ==========================================================================
 * SCAFFOLD -- calls into mp2731_charger_rt1062.c/max17303_fuel_gauge_rt1062.c,
 * same untested-on-real-hardware tier as those files.
 * ==========================================================================
 *
 * Ported byte-for-byte from program_init()'s MP2731/MAX17303 section.
 * Every setpoint value below is a literal transcription -- not derived,
 * not guessed. Commented-out alternates in the original (there were
 * several, e.g. different charge-current limits) are preserved as
 * comments here too, matching the original's own "here's what else was
 * tried" documentation trail, but never executed as active code (they
 * weren't in the original either).
 */

#include "bms_init.h"
#include "mp2731_charger_rt1062.h"
#include "max17303_fuel_gauge_rt1062.h"
#include <stdint.h>
#include <stddef.h>

int bms_init(int *out_board_version)
{
    uint16_t max_register;
    int board_vers;

    /*
     * board_vers is a pre-existing global in the original (not declared
     * in program_init() itself, only assigned via `board_vers=32` when
     * detected) -- its default value BEFORE this detection code runs
     * isn't visible anywhere in what's been pasted. Defaulting to 0
     * here is NOT a confirmed value, just a placeholder that makes the
     * `if (board_vers >= 32)` branch below take the "older board"
     * path unless the 0x4067 signature is actually read back. If your
     * real firmware initializes board_vers to something else (e.g. a
     * specific pre-3.2 revision number) before program_init() runs,
     * update this default to match.
     */
    board_vers = 0; /* TODO VERIFY: see comment above */

    mp2731_rt1062_init();
    max17303_rt1062_init();

    /* Was: max_register = max_read(MAX17303_ADDRESS1, 0x21);
     *      if (max_register == 0x4067) board_vers = 32; */
    if (max17303_read_reg(MAX17303_ADDR_MAIN, MAX17303_REG_BOARD_DETECT, &max_register) == 0) {
        if (max_register == 0x4067u) {
            board_vers = 32;
        }
    }
    /* Was the commented-out `max_write(MAX17303_ADDRESS1, MAX17303_FAULTS, 0);`
     * right after -- never executed in the original, not ported as active code. */

    if (board_vers >= 32) {
        /* Was: max_write(MAX17303_ADDRESS2, MAX17303_nPackCfg, 0x101); //do not use external thermister */
        max17303_write_reg(MAX17303_ADDR_NV, MAX17303_REG_NPACKCFG, 0x0101u);

        /* The original's large commented-out block here (reading back
         * REPCAP/REPSOC/TTF/VCELL/CURRENT/CHARGE_CURRENT with their
         * scaling formulas) was never executed -- dead reference code
         * in the source, not ported as active calls. The scaling
         * formulas themselves (0.5 mAh/LSB, 1/256 %/LSB, 5.625 s/LSB,
         * 0.078125 V/LSB, 0.15625 mA/LSB) are worth keeping on hand if
         * you want a real battery-telemetry readout later -- ask if
         * you want that wired into gps_stub.c-style periodic polling. */

        /* Was: max_write(MAX17303_ADDRESS2, MAX17303_RESISTOR, 0x01F4); // 0.005 ohm resistor : not used in internal calcs */
        max17303_write_reg(MAX17303_ADDR_NV, MAX17303_REG_RESISTOR, 0x01F4u);

        /* Was: max_write(MAX17303_ADDRESS2, MAX17303_JEITAC, 0x64FF); //set charge current to 4.0A  no temp coef changes */
        max17303_write_reg(MAX17303_ADDR_NV, MAX17303_REG_JEITAC, 0x64FFu);
        /* commented out in original: MAX17303_JEITAC, 0xD2FF -- 4.2A alternate, never executed */

        /* Was: max_write(MAX17303_ADDRESS2, MAX17303_FOVCP, 0x2208); //nODSCTh upped the fast overdischarge threshold for modem ops */
        max17303_write_reg(MAX17303_ADDR_NV, MAX17303_REG_FOVCP, 0x2208u);

        /* Was: max_write(MAX17303_ADDRESS2, MAX17303_SOVCP, 0x6480); //nIPrtTh1 no slow over discharge current protect as opened fully. 2's complement so be careful calculating! */
        max17303_write_reg(MAX17303_ADDR_NV, MAX17303_REG_SOVCP, 0x6480u);
        /* commented out in original: MAX17303_SOVCP, 0x37B5 and 0x7F80 alternates -- never executed */

        /* Was:
         *   max_register = max_read(MAX17303_ADDRESS2, MAX17303_OVP);
         *   set_register = 0x70CE;
         *   if (max_register != set_register) {
         *       max_write(MAX17303_ADDRESS1, MAX17303_OVP_BAK, set_register);
         *       max_write(MAX17303_ADDRESS2, MAX17303_OVP, set_register);
         *   }
         * NOTE the address split: OVP is read/written via the NV address,
         * but OVP_BAK is written via the MAIN address -- confirmed from
         * this exact call, not a transcription guess (see
         * max17303_fuel_gauge_rt1062.h's corrected comment on this). */
        {
            uint16_t current_ovp;
            const uint16_t set_register = 0x70CEu; /* UVP=2.7V(25), UOCVP=3.18V(12), UVShdn=2.62V(-2) -- per the
                original's own comment; several other threshold combinations were tried and left as
                comments in program_init(), preserved there in spirit but not repeated verbatim here. */

            if (max17303_read_reg(MAX17303_ADDR_NV, MAX17303_REG_OVP, &current_ovp) == 0
                && current_ovp != set_register) {
                max17303_write_reg(MAX17303_ADDR_MAIN, MAX17303_REG_OVP_BAK, set_register);
                max17303_write_reg(MAX17303_ADDR_NV, MAX17303_REG_OVP, set_register);
            }
        }

        /* Was: set_register = 0xA03D; max_write(MAX17303_ADDRESS2, MAX17303_NVEMPTY, set_register); // VEmpty = 3.2V */
        max17303_write_reg(MAX17303_ADDR_NV, MAX17303_REG_NVEMPTY, 0xA03Du);

        /* Was: MP_Write(MP2731_CHARGE_CURRENT, 0xE0); //max fast charge of 4.16A only set on bootup as POR */
        mp2731_write_reg(MP2731_REG_CHARGE_CURRENT, 0xE0u);
        /* commented out in original: 0xFF (4.52A), 0xC3 (2.98A), 0xB8 (2.56A) alternates -- never executed */
    } else {
        /* Was: MP_Write(MP2731_CHARGE_CURRENT, 0xB4); //max fast charge of 2.4A only set on bootup as POR */
        mp2731_write_reg(MP2731_REG_CHARGE_CURRENT, 0xB4u);
    }

    /* The following run UNCONDITIONALLY regardless of board_vers,
     * exactly matching program_init()'s structure (outside the
     * if/else above). */

    /* Was: MP_Write(MP2731_CURRENT, 0x5C); //max current input 1.5A equates to 18W @ 12V */
    mp2731_write_reg(MP2731_REG_CURRENT, 0x5Cu);
    /* commented out in original: 0x70 (2.4A), 0x1C/0x3F (ILIM off variants), 0x00 (disabled), 0x5F (1.65A) -- never executed */

    /* Was: MP_Write(MP2731_TIMER, 0x85); //turn watchdog timer off to see if it does not reset charge current after 40s */
    mp2731_write_reg(MP2731_REG_TIMER, 0x85u);

    /* Was: MP_Write(MP2731_NTC, 0xDC); //AICO turned off, others default. Saved on POR */
    mp2731_write_reg(MP2731_REG_NTC, 0xDCu);
    /* commented out in original: 0xCC (also disables NTC battery charge) -- never executed */

    /* Was: MP_Write(MP2731_ADC_CONT, 0xD0); //AICO turned off, others default. Saved on POR */
    mp2731_write_reg(MP2731_REG_ADC_CONT, 0xD0u);

    /* Was: MP_Write(MP2731_BATFET, 0x41); //hardware enabled, on time 0.5sec, off 10 sec */
    mp2731_write_reg(MP2731_REG_BATFET, 0x41u);
    /* commented out in original: 0x58 (software enabled variant) -- never executed */

    /* Was: MP_Write(MP2731_CHARGE_VOLTAGE, 0xA0); //set to default 4.2V */
    mp2731_write_reg(MP2731_REG_CHARGE_VOLTAGE, 0xA0u);

    /* Was: MP_Write(MP2731_JEITA, 0xD4); //VHot = 60 degrees, VWarm = 55 degrees Cool = 5  Cold = 0 */
    mp2731_write_reg(MP2731_REG_JEITA, 0xD4u);

    /* Was: MP_Write(MP2731_CHARGE, 0xD4); //Vsysmin 3.3V track 100mV battload enable */
    mp2731_write_reg(MP2731_REG_CHARGE, 0xD4u);
    /* commented out in original: 0x58 (Vsysmin 3.6V variant), 0x54 (Vsysmin 3.3V, no battload) -- never executed */

    if (out_board_version != NULL) {
        *out_board_version = board_vers;
    }
    return 0;
}
