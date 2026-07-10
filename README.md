# ActiveRFID RT1062 Port

Port of the ActiveRFID race-timing firmware from Rabbit/Dynamic C
(RCM6700) to NXP i.MX RT1062, bare-metal (no RTOS), lwIP raw API,
littlefs, MCUboot OTA.

## Layout

- `firmware_source/` -- the actual port: ~119 files. Peripheral drivers
  (`*_rt1062.c`), pure protocol/logic modules (parsers, state machines),
  and app integration (`app_init.c`, `app_loop.c`, `app_pc_dispatch.c`).
- `host_tests/` -- unit tests for the pure-logic modules, buildable and
  runnable on a regular dev machine (gcc), no hardware or NXP SDK
  needed. `cd host_tests && make` runs the full suite.
- `reference_alternatives/` -- code paths superseded during the port
  (e.g. an earlier storage backend), kept for reference rather than
  deleted outright.
- `INTEGRATION.md` / `OTA_MCUBOOT_INTEGRATION.md` -- deeper notes on
  specific subsystems.

## Bring-up status

Peripheral bring-up is staged behind flags in
`firmware_source/bringup_config.h` (`APP_ENABLE_*`, 15 flags) --
enable one at a time when bringing up real hardware, matching the
order noted in that file's comments.

Confirmed against real generated MCUXpresso project files
(`peripherals.c`, `pin_mux.c`, `clock_config.c`): LPI2C1 (DS3231 RTC,
MP2731 charger, MAX17303 fuel gauge), LPUART1/2/5/8 (GPRS modem, Genie
display, reserved FTDI, UHF reader), LPSPI3 (nRF52833, GPS NEO-M8T),
and the full GPIO1/2/3/4 pin map.

## Building the host tests

```
cd host_tests
make
```

Each `test_*` binary is a standalone unit test for one module. No
external dependencies beyond a C compiler.

## Building the real firmware

Not yet part of this repo as a buildable MCUXpresso project -- the
files in `firmware_source/` are meant to be added into a real
MCUXpresso project (created from an SDK example targeting
`evkmimxrt1060`, `flexspi_nor` boot) alongside your own generated
`peripherals.c`/`pin_mux.c`/`clock_config.c`/`board.c` and the
`igpio_adapter`/GPIO Adapter SDK component.
