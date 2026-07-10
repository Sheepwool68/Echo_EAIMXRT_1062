# ActiveRFID RT1062 Port

Porting ActiveRFID race-timing firmware from Rabbit/Dynamic C (RCM6700)
to NXP i.MX RT1062. Bare-metal, **no RTOS**. lwIP raw API, littlefs,
MCUboot OTA.

## Ground rules for this codebase

- **Fidelity over cleanup.** This is a port, not a rewrite. Match the
  original Dynamic C source's behavior exactly, including its quirks,
  unless explicitly told to change something. Don't "improve" logic
  you don't have source for.
- **Never guess at hardware facts.** Pin numbers, peripheral instances,
  register values, polarities -- if it's not confirmed from pasted
  source, a real generated MCUXpresso file, or explicit instruction,
  flag it as a placeholder in a comment rather than inventing a
  plausible-looking value. Search prior comments for `TODO`, `NOT
  CONFIRMED`, `FLAGGED` before assuming something is settled.
- **Test after every change.** `cd host_tests && make` -- 32 suites,
  all pure-logic modules (parsers, protocol state machines, BCD/epoch
  conversion, etc). Should stay green. Hardware-touching files
  (`*_rt1062.c`) aren't part of this suite -- verify those with
  `gcc -fsyntax-only` against the stub headers in `host_tests/test_stubs/`
  when no real SDK is available.
- **Check both extremes of `firmware_source/bringup_config.h`** (all
  `APP_ENABLE_*` flags off, all on) after touching anything gated by
  those flags -- easy to break one path while fixing the other.

## Confirmed hardware (do not re-derive -- these came from real pasted
## peripherals.c/pin_mux.c/clock_config.c, not guesses)

**LPI2C1** (shared bus, 60MHz clock, 100kHz baud): DS3231 RTC (0x68),
MP2731 charger (0x4B), MAX17303 fuel gauge (two addresses). Non-blocking
transfer + callback + spin-wait pattern (NOT `LPI2C_MasterTransferBlocking`)
-- see `lpi2c1_bus_rt1062.c`, confirmed against the user's own tested
I2C.c.

**LPUART instances**: LPUART1=GPRS modem, LPUART2=Genie 4D display,
LPUART5=FTDI USB-serial (reserved, no code yet), LPUART8=UHF RFID reader.
80MHz clock root.

**LPSPI3** (shared bus, 105.6MHz clock, 500kHz baud, SPI Mode 0):
nRF52833 (manual GPIO CS -- `GPIO_AD_B1_12`/GPIO1 pin 28) and GPS
NEO-M8T (manual GPIO CS -- `GPIO_AD_B0_04`/GPIO1 pin 4). CS is
deliberately NOT hardware PCS -- reverted from an earlier hardware-PCS
attempt per explicit instruction ("the LPSPI driver only handles one CS
pin").

**GPIO1** pins: PPS=2 (GPS, rising edge), TIMEPULSE=3 (DS3231 seconds
pulse, falling edge -- distinct signal from PPS), FAN_CONTROL=20
(active high, on/off tied to UHF reader start/stop), BUZZER=21 (active
high), MODEM_PWR=22, TRIGGER=23 (purpose still unknown -- see Open
items), MODEM_DTR=24 (boot-level conflict, see Open items).

**GPIO3** pins: READER_SHUTDOWN=15 (active LOW), READER_PWR=17 (active
HIGH) -- these two always move together via `reader_power_set()`, never
the per-pin primitives independently. NRF_READY=2. BUTTON_LED=4.

Two pin sources overlap for PPS/TIMEPULSE/MODEM_DTR: `pin_mux.c`'s raw
`GPIO_PinInit()` AND `peripherals.c`'s HAL adapter `HAL_GpioInit()`.
Harmless redundancy (HAL adapter runs second, wins) -- don't "fix" it,
it's how MCUXpresso Config Tools generates things when a pin is in both
the Pins tool and the Peripherals tool's GPIO group.

## Architecture patterns established during this port

- **HAL GPIO adapter** (`fsl_adapter_gpio.h`, `HAL_GpioInit`/
  `HAL_GpioSetOutput`/etc) for pins configured via the Peripherals tool
  -- NOT raw `fsl_gpio.h` for those specific pins. Raw `fsl_gpio.h`
  still used for pins not in that generated set (e.g. READER_SHUTDOWN/
  READER_PWR).
- **Shared buses get one init, not one per consumer.** `BOARD_InitPeripherals()`
  initializes LPI2C1/LPUART*/LPSPI3 once; driver files must NOT
  redundantly call `LPI2C_MasterInit()`/`LPSPI_MasterInit()`/etc
  themselves. `lpi2c1_bus_rt1062.c` is the shared I2C transfer helper
  three device drivers call into.
- **Non-blocking state machines, not blocking loops with timeouts**,
  for anything that was a Dynamic C cofunc in the original (yields
  every tick, never blocks the caller). See `outreach.c` (was
  `outbound_connect.c`, renamed -- "Outreach" is the real protocol name
  for modem/LAN-to-server connection, unrelated to FinishLynx) for the
  reference pattern: `_begin()` to start, `_step()` called every main
  loop iteration to advance one non-blocking increment,
  `_in_progress()` to check status.
- `HAL_GpioSetOutput()`/`HAL_GpioGetInput()` names are **best-effort
  guesses**, never confirmed against a real output-write or input-read
  example -- flagged in every file that uses them.

## Open items (unresolved as of last session)

- `TRIGGER` pin (GPIO1 pin 23): primitive built (`trigger_input_read()`),
  behavior unknown -- no source for what `Settings.TriggerOn` does, or
  whether it needs edge detection vs polling.
- `USB_STATUS` (GPIO4 pin 0), `USB_DTR` (GPIO2 pin 16), `BUTTON_LED`
  (GPIO3 pin 4): no drivers built, purposes are guesses from pin names
  only.
- `MODEM_DTR` boot-level conflict: original source sets it LOW at boot
  ("wakeup"), generated `peripherals.c` initializes it HIGH. Unresolved.
- `LANSERVER_CONN_TIMEOUT_MS`, `board_vers` pre-detection default,
  MAX17303 register `0x21`'s real datasheet name, UHF start/stop's
  exact display form/LED reset behavior: all flagged defaults, never
  confirmed.
- littlefs and lwIP are not actually linked in -- `host_tests/` uses
  stub headers for these; a real MCUXpresso project needs the genuine
  libraries.

## Directory layout

- `firmware_source/` -- the port itself (~90 files)
- `host_tests/` -- pure-logic unit tests, runs on any machine with gcc
- `reference_alternatives/` -- superseded code paths kept for reference
- `INTEGRATION.md`, `OTA_MCUBOOT_INTEGRATION.md` -- deeper subsystem notes
