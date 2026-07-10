/*
 * bms_init.h
 *
 * Was the MP2731/MAX17303 section of program_init() -- ported faithfully
 * from the actual pasted source (previously only register-level
 * primitives existed here, since program_init() itself wasn't available
 * yet). Board-revision-gated setpoints preserved exactly, including the
 * commented-out alternates left as comments (not ported as active code --
 * they were never executed in the original either).
 */

#ifndef BMS_INIT_H
#define BMS_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Was the MP2731/MAX17303 section of program_init(), from
 * `genieWriteStr(GENIE_SPLASH_STR, "Initialise MP2731");` through the
 * end of the charger/fuel-gauge setpoint writes (DS3231 setup is
 * handled separately by ds3231_rt1062_init(), which now also reflects
 * program_init()'s confirmed CONTROL-register sequence).
 *
 * *out_board_version receives the detected board revision (32 for
 * "V3.2 boards" per the original's own comment, matching a specific
 * MAX17303 register-0x21 readback; otherwise whatever board_vers's
 * pre-existing default was -- see this file's .c comment on why that
 * default is flagged, not guessed). Pass NULL if you don't need it.
 *
 * Returns 0 always (matches the original's own lack of any error
 * path here -- none of MP_Write()/max_write()'s return values were
 * checked in program_init() either).
 */
int bms_init(int *out_board_version);

#ifdef __cplusplus
}
#endif

#endif /* BMS_INIT_H */
