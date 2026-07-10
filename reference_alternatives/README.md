# reference_alternatives/

Files here are **not part of the active firmware build** and must not be
added to your MCUXpresso project alongside `firmware_source/`.

## nand_log_fatfs.c / nand_log_fatfs.h

An earlier architectural draft of the record-log storage layer, written
against FatFs over SD/eMMC before the project settled on littlefs over
QSPI NOR flash instead (see `nand_log_littlefs.h`'s header comment, and
`app_context.h`/`gprs_batch_sender.h`/`nand_log_flash_qspi.h`, all of
which use the littlefs version exclusively).

It was never deleted from the file set, and it declares the **same
public function names** as `nand_log_littlefs.c/h`
(`nand_log_reset`, `nand_log_open_for_read`, `nand_log_close`, etc).
Compiling both into the same project will fail at link time with
duplicate-symbol errors.

Kept here only as a reference in case you ever want to switch to
FatFs/SD storage instead of littlefs/QSPI-NOR -- if you do, you'd swap
it in and remove `nand_log_littlefs.c/h` from the active build, not add
both. It has not been touched or re-verified since the littlefs switch;
treat it as an unmaintained starting point, not a currently-correct
alternative.
