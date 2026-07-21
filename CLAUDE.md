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
LPUART5=FTDI USB-serial debug console (CONFIRMED WORKING on real
hardware -- `debug_printf()`/`PRINTF` redirect, see
`debug_console_rt1062.c`/`lpuart5_console_rt1062.c`; also where
semihosting was fully removed at the linker level), LPUART8=UHF RFID
reader. 80MHz clock root.

**LPSPI3** (shared bus, 105.6MHz clock, 1MHz baud, **SPI Mode 1**
CPOL=0/CPHA=1 -- corrected 2026-07-14 from Mode 2, see
`project_gps_spi_bringup` memory for the full investigation; matches
the original Dynamic C's own commented-out `SPImode(2)` line
annotated "for some reason is mode 1 in NRF52833"): nRF52833 (manual
GPIO CS -- `GPIO_AD_B1_12`/GPIO1 pin 28) and GPS NEO-M8T (manual GPIO
CS -- `GPIO_AD_B0_04`/GPIO1 pin 4). Baud rate confirmed 2026-07-13
against the NEO-M8 datasheet's 125kB/s max SPI rate. CS is
deliberately NOT hardware PCS -- reverted from an earlier hardware-PCS
attempt per explicit instruction ("the LPSPI driver only handles one CS
pin"). GPS SPI activity must precede any nRF SPI query in the same
boot (a real, empirically-discovered quirk from the original source,
not a bug) -- see `neo_m8t_transport_rt1062_init()`'s bus-priming
usage in `app_init.c`.

**GPIO1** pins: PPS=2 (GPS, rising edge), TIMEPULSE=3 (DS3231 seconds
pulse, falling edge -- distinct signal from PPS), FAN_CONTROL=20
(active high, on/off tied to UHF reader start/stop), BUZZER=21 (active
high), MODEM_PWR=22 (RESTORED 2026-07-17 to its real, original GPRS
modem-power-enable function -- was briefly repurposed to drive the
button LED, per an incorrect early guess, then corrected back once the
user provided the actual schematic reference showing the LED's real
pins live on GPIO3, not here; modem power itself is hardwired
always-on externally on this board regardless), TRIGGER=23 (purpose
still unknown -- see Open items), MODEM_DTR=24 (boot-level conflict,
see Open items).

**GPIO3** pins: BUTTON_LED_PIN1=16 (GPIO_SD_B0_04, CONFIRMED 2026-07-17
-- schematic-correct per the user's explicit clarification
("GPIO_SD_B0_05-SD1_D3 is one pin", confirming each hyphen-joined string
in the schematic photo names ONE pin by its two alternate names, not
two pins) AND independently confirmed against
`drivers/fsl_iomuxc.h`'s `IOMUXC_GPIO_SD_B0_04_GPIO3_IO16` macro (ALT5).
This pin has NO entry anywhere in `board/pin_mux.c` -- unlike
READER_SHUTDOWN/READER_PWR below, which do -- so `button_led_rt1062_init()`
now calls `IOMUXC_SetPinMux()`/`IOMUXC_SetPinConfig()` itself before
`GPIO_PinInit()`; the earlier "1.2V floating" symptom on this exact pin
number was caused by relying on `GPIO_PinInit()` alone, which never
touches the pad's mux-select, leaving the pin on its power-on-reset
ALT0 default (`USDHC1_DATA2`) rather than routing it to GPIO -- not a
software logic bug. See `BUTTON_LED` Open Items entry for the full
4-round correction story, including a "no change" dead end at GPIO3
pin 4 (a DIFFERENT bank, `GPIO_SD_B1_04`, chasing a pre-existing but
apparently-stale `pin_mux.c` label of the same name). READER_SHUTDOWN=15
(active LOW), READER_PWR=17 (active HIGH, REPURPOSED 2026-07-17 to drive
`BUTTON_LED`'s pin 2 instead on this particular board -- CONFIRMED
correct throughout, never in question; `reader_power_set()` no longer
writes it, only `reader_shutdown_set()`) -- previously these two always
moved together via `reader_power_set()`. NRF_READY=2. Earlier abandoned
pin-1 guesses: GPIO3 pin 4 (twice, for different reasons), MODEM_PWR
(GPIO1 pin 22) -- see the `BUTTON_LED` Open Items entry for the full
correction history.

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

## Status as of 2026-07-15

The real entry point (`main.c` -> `app_init()` -> a bare
`app_run_one_iteration()` loop) is now wired into the actual
MCUXpresso project for the first time, replacing the old hand-written
`hello_world.c` per-driver bring-up sequence entirely. Confirmed
working on real hardware: the full Genie touchscreen UI dispatcher
(`app_genie_dispatch.c`, the `myGenieEventHandler` port -- forms,
knob/trackbar/slider/4Dbutton/winbutton, keyboard/keypad text entry,
PIN/admin gating) correctly routes real touch events to the right
widget handlers on the actual MAIN screen (not the splash screen,
which has no touchable widgets -- a boot-sequence bug where the
display never left the splash form, fixed the same day).

`firmware_source/bringup_config.h` is now staged to **Genie + TCP +
BMS + Time Sync + GPS** (`APP_ENABLE_DISPLAY=1`, `APP_ENABLE_TCP=1`,
`APP_ENABLE_BMS=1`, `APP_ENABLE_TIME_SYNC=1`, `APP_ENABLE_GPS=1`, every
other flag off, including `APP_ENABLE_BOARD_IO` gating buzzer/reader-
shutdown/fan GPIO inits) as of 2026-07-16. Time Sync (DS3231 RTC) was
added first -- read-only I2C on the same bus already confirmed live
via BMS, previously confirmed standalone via the DS3231's 1Hz
TIMEPULSE ISR -- **CONFIRMED WORKING on real hardware same day**, user
reported the Genie MAIN screen's time visibly incrementing (confirms
both the boot-time RTC read and the ongoing ISR-driven rollover
update). Unlike TCP/BMS, `app_init.c`'s `APP_ENABLE_TIME_SYNC` blocks
have real `return -1` failure paths (`ds3231_rt1062_init()`/
`ds3231_rt1062_read()`) that would halt boot on failure -- an
acceptable risk here since DS3231 comms are already independently
confirmed on this exact I2C bus, and hardware confirmation shows it
wasn't an issue.

GPS was added next, per explicit request to get GPS-driven time sync
(with timezone applied) showing correct date/time on the LCD.
`app_loop.c`'s `process_time_sync()` already had this fully wired from
an earlier session -- `gps_sync_time_from_fix(&gps_time, app->settings.
time_zone, app->settings.add30)` already applies the timezone/half-hour
offset, and writes the result to both the DS3231 and the Genie
display's hour/minute digits + date string -- so this was purely a
flag flip, no new code. GPS auto-time-sync itself is separately gated
by the touchscreen's `auto_set_gps_time` toggle
(`pps_should_trigger_gps_sync()`), which defaults OFF -- faithful to
the original's own `Settings.AutoSetGPSTime = 0` boot default. Since
`APP_ENABLE_STORAGE` isn't staged yet, this setting doesn't persist
across reboots, so the touchscreen toggle needs tapping after every
boot until storage comes up. No shared-LPSPI3-bus conflict with
`APP_ENABLE_NRF_SPI` being off -- the GPS-bus-priming-before-nRF-query
concern only matters when both run in the same boot.

**GPS signal-status readout decoupled from `gps_time_set`, 2026-07-16.**
User reported PPS visibly toggling with a confirmed good fix, but no
GPS tank-bar activity on the Genie MAIN screen and no time set. Traced
byte-for-byte against `NEOM8T.lib`'s `PVT_report()`/`GetGPS_Signal()`:
both symptoms are faithful to the ORIGINAL firmware's own design, not
a porting bug -- `pvt.status` (gating both the time-sync trigger and,
originally, the sat-bar update) requires a real `fixType>0` +
`gnssFixOK`, not just PPS/time-validity (u-blox modules can toggle PPS
once they have time validity, before a full nav fix); and the
original's own `GetGPS_Signal()` call is *itself* gated behind
`if(set_time)` (`ACTIVERFID_V1.02_UHF.c` line 3825) -- same circular
"can't see satellites until time is already set" design this port
faithfully carried over. That circularity is genuinely bad for bring-up
diagnostics, so per explicit request it was decoupled: `app_loop.c`'s
`process_periodic_checks()` now calls `gps_update_signal_status()` on
its own 30s timer (`GPS_SIGNAL_CHECK_MS`, first fire 15s after boot,
both values unchanged/faithful) regardless of `gps_time_set`, gated
only on `APP_ENABLE_GPS` at compile time (not runtime -- calling it
with that flag off would lazily arm the GPS SPI transport even when
the stage is supposed to be inert). Also added a debug-UART trace
(`"GPS signal: %d sats, status=%d, pps=%d"`) of the RAW satellite
count and fix status -- neither was observable anywhere before (the
Genie tank widget only shows a scaled 0-100 bar, and even that
required `APP_ENABLE_DISPLAY` and physically reading a bar's height).
`neo_m8t_update_signal_status()`/`gps_update_signal_status()` both
gained optional out-params (`int *out_raw_sats, int *out_status`,
either may be `NULL`) to expose this -- backward compatible, existing
callers pass `NULL, NULL`. Host test `test_neo_m8t_reader.c` updated
for the new signature and extended to assert the raw sat count/status
values too, not just the scaled tank percentage.

**Full boot-order audit against the real `main()`, 2026-07-16, per explicit request** ("follow the bringup order in my dynamic C main() prior to the loop state machine"). Read the ENTIRE original pre-loop sequence end to end: `LoadSettings(); program_init();` at the top of `main()` (`ACTIVERFID_V1.02_UHF.c` line 3458), all of `program_init()` itself (lines 2586-2772 -- GPIO setup, `genie_startup()`, `SetRabbitIP()`, `SPIinit()`, `MountNANDLogFile()` very early, `i2c_init()`, board-version detect + BMS/MP2731 setpoints, DS3231 hardware register config, MAC address, firmware info), then the rest of `main()` up to `for(;;)` (fan/nRF-fw-version/splash sequence, DS3231 ISR install, `TCPIPOpenSockets()`, GPRS timer/httpc init, `set_UBX()`, `Toggle_Modem()` if remote off, PPS ISR install, `genieActivateForm(MAIN)`/`updateGenie_Main()`, system-mode branch, the Rabbit-internal-clock RTC sync, `TCPIPOpenSocket_FinishLynx()`, abnormal-shutdown UHF resume). Cross-checked line by line against the current `app_init()`.

**Found and fixed one more real, higher-impact bug from this audit**: `APP_ENABLE_STORAGE`'s mount + `settings_store_load()` block was sitting at the very END of `app_init()` -- after the nRF channel push, BMS writes, and the system-mode branch had already run using nothing but hardcoded defaults. The original mounts NAND and loads settings essentially first thing (`MountNANDLogFile()` is one of the first calls inside `program_init()`, and `LoadSettings()` runs even before `program_init()`), specifically because everything downstream depends on real settings being loaded, not defaults. This bug was dormant (APP_ENABLE_STORAGE has been off for every hardware pass so far) but would have silently applied wrong nRF channel/reader-power/system-mode values the moment storage was enabled, with nothing crashing to reveal it. Fixed by moving the whole mount+load(+save-if-first-boot) block to run immediately after the hardcoded-defaults assignment, before anything else touches `app->settings` -- left the `APP_ENABLE_UHF && APP_ENABLE_STORAGE` abnormal-shutdown-resume check in its original late position (matches the original's own `UHF_Reader_Control(1)` being the very last substantive action before the loop). Verified clean at multiple flag combinations against the real SDK, including a combined `STORAGE=1`+`UHF=1` test to exercise both the moved block and the still-late resume check that depends on it.

**Two smaller ordering divergences noted but deliberately left alone** (reviewed, not fixed, since they have no confirmed functional impact -- flagging for the record rather than silently deciding): (1) BMS setpoint writes happen before DS3231 hardware config in the original (both inside `program_init()`), but this port's `APP_ENABLE_TIME_SYNC` block (DS3231) currently runs before `APP_ENABLE_BMS` -- independent I2C register writes to different device addresses on the same already-initialized bus, no observed or expected behavioral difference either order. (2) `set_UBX()` (GPS) runs after the GPRS timer/`httpc_init()` in the original, but this port's `APP_ENABLE_GPS` block currently runs before `APP_ENABLE_GPRS` -- independent SPI-vs-UART subsystems with no shared resource. Revisit either if a concrete reason to care about them surfaces.

**Second, more exhaustive boot-order pass against the real Dynamic C
source, 2026-07-16, per explicit (and pointed) instruction** ("Again
you must follow exactly how I used to bring things up!!! You have my
source, follow it!") after the first audit's fixes turned out
incomplete. This pass went deeper: re-read every `Settings.X`
assignment in `program_init()`'s real `LoadSettings()`-equivalent
default block (`ACTIVERFID_V1.02_UHF.c` lines 2515-2569) field by
field against `app_init()`'s hardcoded-defaults block, not just the
call-order of major subsystems.

**Found and fixed two more real, higher-impact bugs**:
1. **`SetRabbitIP()` -- the original's boot-time DHCP-vs-static-IP
   branch on `Settings.useDHCP` -- was entirely MISSING from the port's
   boot sequence.** `enet_lwip_rt1062_init()` used to unconditionally
   `dhcp_start()` internally, completely ignoring
   `app->settings.use_dhcp`/`rabbit_ip`/`rabbit_gateway` no matter what
   was saved to flash -- the board would always attempt DHCP at boot
   regardless of a saved static-IP configuration, with no way to reach
   static-IP mode except re-toggling the touchscreen after every single
   reset. Fixed: removed the internal `dhcp_start()` call from
   `enet_lwip_rt1062_init()` (now purely raw PHY/pin/clock/netif setup,
   matching the original's clean separation between `program_init()`
   and `SetRabbitIP()`), and made `app_init()` call the already-existing
   `enet_lwip_rt1062_apply_network_settings()` immediately afterward,
   using the real (now correctly pre-loaded, thanks to the earlier
   storage-ordering fix) `app->settings` values. This function already
   existed for the touchscreen's live-reconfig path; it just was never
   also called at boot.
2. **Two settings defaults were wrong/missing, one of them CRITICAL
   given fix #1**: the original's real first-boot defaults are
   `Settings.RabbitIP[] = {192,168,1,90}` and
   `Settings.SendDataToRemoteServer = 1` (confirmed, lines 2529/2535) --
   neither was ever explicitly set in `app_init()`'s defaults block, so
   both silently fell back to `memset`'s zero (`0.0.0.0` and `0`/off
   respectively). `send_data_to_remote_server` defaulting wrong was a
   latent, lower-severity bug. `rabbit_ip` defaulting to `0.0.0.0`
   became CRITICAL the moment fix #1 landed in the same change --
   without also fixing this, a genuinely fresh device (or one with a
   failed settings-file validation) would have configured its static-IP
   fallback to `0.0.0.0`, i.e. no usable network at all. Fixed in the
   same commit as #1, not as a separate follow-up. Also added explicit
   `use_dhcp=0`/`add30=0` (both already correct via `memset`, but now
   stated explicitly given how central `use_dhcp` is to the new boot
   logic). Cross-checked EVERY remaining `Settings.X` default in that
   original block against its port field this time (`DataOnRequest`,
   `RemoteType`, `TriggerOn`, `Dim`, `UHF_region`, `System`,
   `ShutDownStatus`/`NORMAL_SHUTDOWN`) -- all confirmed to already
   correctly default to 0 via `memset`, nothing else missing.

Verified `-fsyntax-only -Wall -Wextra` clean against the real SDK at
current real flags AND a temporary `APP_ENABLE_TCP=0` test, real flags
confirmed restored after.

**Battery percent boot-order bug found and fixed, 2026-07-16, per
explicit request** ("bat% bar and value need to update fast before
loop") -- same class of bug as the DS3231-before-display fix above.
`app->batt_percent` (feeding the Genie battery gauge/digits AND the
TCP `V=%d\n` line) was only ever refreshed by
`process_periodic_checks()`, which doesn't fire until `BATTERY_CHECK_MS`
(2000ms) into the main loop -- so the very first LCD paint at boot
always showed a stale/zero value. Extracted the MAX17303_REG_REPSOC
read (previously inline in `process_periodic_checks()`) into a new
shared function, `app_update_battery_percent(app_context_t *app)`
(declared in `app_context.h`, defined in `app_loop.c`, matching the
existing cross-file pattern used by `app_beep()`/`app_run_one_iteration()`).
`app_init()` now calls it once immediately, right alongside the
DS3231-read fix, before the display paint; `process_periodic_checks()`
now just calls the same function on its existing repeat timer instead
of duplicating the logic. No-op unless `APP_ENABLE_BMS` is on AND
`board_version>=32`, same gating as before. Verified `-fsyntax-only
-Wall -Wextra` clean against the real SDK at current real flags AND a
temporary `APP_ENABLE_BMS=0` test (exercises the new function's
`#else (void)app;` branch), real flags confirmed restored after.

**`APP_ENABLE_STORAGE` flipped on + GPS-time-sync made a real persistent
setting, 2026-07-16, per explicit request** ("Set time by GPS is a
persistent setting. Make it that now. I do not want to have to toggle
it each flash."). Two changes: (1) `app_init.c`'s hardcoded-defaults
block now sets `app->settings.auto_set_gps_time = 1` -- a DELIBERATE
deviation from the original's own confirmed `Settings.AutoSetGPSTime =
0` default, explicitly flagged as such in the code, not a silent
"correction." Only matters on a genuinely first boot or a failed
settings-file validation; once a real settings file exists, whatever's
actually saved (including a manual toggle back to 0 via the
touchscreen, which already calls `app_persist_settings()`) wins, same
as every other field. (2) `APP_ENABLE_STORAGE` flipped 0->1 -- without
it, nothing persists across reflashes regardless of any default.
Well-timed: the app_init.c storage-ordering bug (see above) that would
have bitten this exact flag was found and fixed earlier the same
session via the full boot-order audit. Verified `-fsyntax-only
-Wall -Wextra` clean against the real SDK with the actual now-staged
flag combination (Display+TCP+BMS+TIME_SYNC+GPS+STORAGE). **Honest
caveat**: `settings_store.c`'s `/settings.dat` file path has never
been exercised on real hardware before -- littlefs itself was
confirmed working in an earlier standalone session, but only via
`/ACTIVERFID.LOG` (the tag-read log), not this settings file; only
unit-tested (host_tests) until this flash.

**Real boot-order bug found and fixed, 2026-07-16: DS3231 time wasn't
showing on the LCD immediately at power-up** (user's expectation,
correctly -- DS3231 is battery-backed and retains real time across
power cycles; GPS only ever RE-sets it, doesn't establish it). Root
cause: `app_init.c` called `display_activate_form(GENIE_FORM_MAIN);
app_genie_update_main(app);` BEFORE `ds3231_rt1062_read(&app->
current_time)`, so the MAIN screen's first paint used
`app->current_time` while it was still the `memset()` zero-default,
not real DS3231 data -- only self-corrected a second or so later, on
the first TIMEPULSE rollover. Confirmed against the original: its
`updateGenie_Main()` does its OWN `mytime = RTC_Get()` internally
(`ACTIVERFID_V1.02_UHF.c` line 1470), always self-reading fresh RTC
data right before painting -- this port split "read RTC" and "display
it" into separate steps (`app->current_time` as a shared cache,
consistent with `process_time_sync()`'s own rollover-driven refresh
design), which is sound architecture but meant call ORDER in
`app_init()` now matters where the original had no such dependency at
all. Fixed by moving the `APP_ENABLE_TIME_SYNC` block (the
`ds3231_rt1062_read()` call) to run BEFORE the `APP_ENABLE_DISPLAY`
block. Verified compiling clean at both `APP_ENABLE_TIME_SYNC`
extremes. Sequence this
session: Genie-only pass confirmed working end-to-end 2026-07-15, then
`APP_ENABLE_TCP` added and **confirmed booting on real hardware**
(`enet_lwip_rt1062_init()` is fully gated behind this flag in
`app_init.c` -- unlike the LPI2C1 gotcha below, `board/peripherals.c`
has no Config-Tools-generated unconditional ENET init to worry about).
Two one-shot debug-UART diagnostics were added to `app_loop.c` after
"no lease acquired" turned out ambiguous between no-PHY-link and
link-fine-no-DHCP-server: `ENET link up` and
`DHCP lease acquired: x.x.x.x`. Then, after the user reported the TCP
`V=%d\n` battery-status line reading `V=0` (expected ~100), traced the
cause: `app->batt_percent` is only ever populated once
`app->board_version` (set by `bms_init()`'s real 0x4067 board-revision
I2C read) is >= 32 -- with `APP_ENABLE_BMS` off, `board_version` stays
at its 0 default, silently zeroing both the TCP battery line and the
Genie screen's own battery gauge. Also found and fixed a real,
independent gap while there: `process_periodic_checks()` had a stale
`TODO: battery ADC read (MP2731/charger library not provided)` -- the
MAX17303 driver has existed for a while; nobody had wired the periodic
`MAX17303_REG_REPSOC` read in. Added it, faithfully porting the
original's scaling formula (raw * 1/256, +1 fudge, capped at 100) --
gated on `board_version >= 32` same as the original. Deliberately NOT
ported: the original's `!BitRdPortI(PBDR,3)` (PB3/NRF_READY, GPIO3
pin 2) gate on this same check -- that pin is only configured as GPIO
input under `APP_ENABLE_NRF_SPI`, and it's genuinely unclear whether
it was a real bus-safety interlock or an artifact of the original's
own tight scheduling loop; flagged rather than guessed. User then gave
explicit consent to flip `APP_ENABLE_BMS` on (writes real
charge-current/voltage/protection setpoints to the MP2731/MAX17303,
same write sequence already confirmed safe 2026-07-14). All changes
syntax-checked clean (`-fsyntax-only -Wall -Wextra`) against the real
SDK tree with the actual now-staged flag combination; **not yet
rebuilt/reflashed** -- next step is to confirm the TCP greeting/status
line and a real (~100, not 0) battery percentage on hardware.
Remaining subsystems (nRF SPI, RTC/time-sync, storage, UHF, rewind,
GPRS, GPS) have each been individually confirmed working via
standalone tests in earlier sessions, but have never run together
through this actual `app_init()`/`app_loop()` call sequence. Bring
them back up ONE flag at a time, not all at once.

**ENET/MDIO hang -- ROOT CAUSE FOUND AND RESOLVED, 2026-07-16, end of session.** After the boot-order audit fixes above (storage-load-order, DS3231-read-order, battery-percent-order, SetRabbitIP()-equivalent) landed together with `APP_ENABLE_STORAGE` flipped on, `app_init()` started hanging silently inside `enet_lwip_rt1062_init()`. Full diagnostic arc (boot-progress checkpoints, a genuinely missing SDK compiler define, a revert-and-retest, a stale-IDE-indexer false alarm) is documented in detail in project memory ([[project_enet_lwip_bringup]], not this repo) -- summary:

1. Found and fixed a second missing-compiler-define landmine (same class as `FSL_FEATURE_PHYKSZ8081_USE_RMII50M_MODE`): `fsl_enet.c`'s `ENET_MDIOWaitTransferOver()` falls back to a genuinely unbounded `while(){}` without `ENET_MDIO_TIMEOUT_COUNT` defined. Added `=100000` to both build configs in the real project's `.cproject`. **Kept permanently** -- real, independent robustness fix, though it alone did not resolve the hang.
2. Reverted `APP_ENABLE_STORAGE` to 0 (plus an in-flight, never-hardware-tested `enet_lwip_rt1062_apply_network_settings()` boot-time-call experiment) per explicit "go back to what worked" instruction. Hang persisted identically -- **Storage is confirmed innocent**, was a correlated red herring, not the cause.
3. **Real root cause**: once the MDIO wait became bounded, `PHY_Init()` was found to be genuinely, repeatedly failing (not hanging) -- tripping lwIP's `LWIP_ASSERT` into `sys_arch.c`'s deliberate `for(;;){}` halt, invisible on LPUART5 because that assert path uses the default (non-redirected) `PRINTF`, not `debug_printf()`. Root-caused to the user's own recently-updated **LinkServer debug-probe firmware** interfering with `GPIO_AD_B0_09`/`10` (ENET_RST/ENET_INT, dual-purpose with `JTAG_TDI`/`TDO`) during `enet_phy_hw_reset()` -- **confirmed by testing with the debug probe physically disconnected: boot now completes fully**, real DHCP lease obtained (`192.168.0.152`), all `app_init()` checkpoints through `COMPLETE`.

`APP_ENABLE_STORAGE` is still `0` in the current staged flags (reverted during the investigation, not yet re-enabled since the real fix landed) -- safe to re-flip given Storage's innocence is now confirmed, but per this file's own explicit-confirmation discipline, ask before doing so; the earlier storage-load-ordering fix still needs its own first hardware confirmation once re-enabled.

**Follow-up: MAIN still stuck on splash for a genuine cold power-up (no debug probe attached), 2026-07-16.** The earlier "CONFIRMED WORKING" test below was done via probe-attached reflash-and-run, which may not be a genuine cold boot (display could already be warm from a prior test). User reported: power-cycled with no probe attached, screen stayed on splash. Root cause: `app_init()`'s `display_activate_form(GENIE_FORM_MAIN)` call is one-shot -- `genie_write_object()`'s own `!display_detected` guard silently drops it if the display wasn't detected yet at that exact moment, which is far more likely on a true cold boot (display starts booting from scratch alongside the RT1062) than a probe-attached warm reflash. The display's own ongoing auto-ping (`genie_ping()`, called every `display_do_events()`/main-loop iteration) DOES recover the link moments later -- but nothing ever retried the MAIN-activate command after that recovery, so the screen stayed stuck even once comms came back. Fixed: added `display_is_online()` (`display_stub.h/.c`, wraps `genie_online()`) and a new `app->display_main_shown` flag (`app_context.h`); `app_loop.c`'s main loop now retries `display_activate_form(GENIE_FORM_MAIN)` every iteration until it actually succeeds (`!app->display_main_shown && display_is_online()`), instead of relying solely on winning the boot-time race. `app_init()`'s own one-shot call still fires first (unchanged) and marks the flag done immediately if it happened to land. Same resilience-fix pattern as the ENET non-fatal-failure change earlier this session -- make a real boot-timing race recoverable, don't just guess at a bigger fixed delay. Verified `-fsyntax-only -Wall -Wextra` clean for `app_init.c`, `app_loop.c`, `display_stub.c` together. **Not yet hardware-tested with a genuine cold power-up.**

**Follow-up bug in the retry fix itself, same day: marked "done" without checking the write actually succeeded.** After the display-retry fix above, user confirmed the retry DID fire (`Display: link online, activating MAIN now` appeared) but the splash screen still never showed any content (not even the boot-time `display_show_splash("Starting up")` text) -- pointing at the WRITE itself failing, not the detection-timing race. Traced: `genie_write_object()` (`genie_display.c`) can return -1 (timeout, no ACK within `genie_cmd_timeout`==1250ms) even when `display_detected` is already true (e.g. racing an in-flight auto-ping read) -- but `genie_activate_form()`/`display_activate_form()` were both `void`, discarding that result all the way up. Both the app_loop.c retry AND app_init.c's original boot-time call were marking `app->display_main_shown = 1` unconditionally (or based on `display_is_online()`, which only proves detection, not that THIS write landed) -- so a single failed write got permanently marked "done" and never retried, explaining the still-stuck screen despite the retry firing. Fixed: `genie_activate_form()`/`display_activate_form()` changed from `void` to `int` (1=ACK, 0=NAK, -1=timeout/not-detected -- all ~20 existing call sites elsewhere in `app_genie_dispatch.c`/`app_pc_dispatch.c`/`app_loop.c` are unaffected, discarding a return value is valid C). Both call sites (app_init.c's boot-time attempt, app_loop.c's retry) now only set `display_main_shown` on an actual `==1` (ACK) result. Also added a 2000ms cooldown (`app->display_main_retry_ms`) to app_loop.c's retry -- without it, a failure that DOESN'T clear `display_detected` would retry on literally every main-loop iteration, each blocking up to 1250ms waiting for an ACK, the same "retry storm blocking the main loop" class of bug already found and fixed once this session for GPS's PPS-triggered time-sync retries. Verified `-fsyntax-only -Wall -Wextra` clean across `app_init.c`, `app_loop.c`, `display_stub.c`, `app_genie_dispatch.c`, `app_pc_dispatch.c`. **Not yet hardware-tested.**

**CONFIRMED WORKING on real hardware, 2026-07-16 (probe-attached test): MAIN form now shows correctly on boot.** Both the display-delay fix and the ENET-resilience fix (below) are now hardware-confirmed together -- full boot completes (`COMPLETE, entering main loop`), the Genie display link is alive and responding continuously (`Sending Read Form as Ping` / `Got Current Form` auto-ping cycle), and the physical screen shows MAIN, not splash. This closes out the two biggest blockers from this session.

**Splash-screen-not-transitioning-to-MAIN issue found immediately after, same session -- fixed with a whole-board boot delay, not a display-specific one.** With boot now completing fully (confirmed via checkpoints including `DISPLAY activate MAIN done`), the Genie touchscreen was still showing splash, not MAIN. Root cause, this time a CONFIRMED original-source value (not a guess -- an earlier pass on this same question wrongly concluded no such value existed, see [[feedback_exhaustive_source_fidelity]] for that mistake): `ACTIVERFID_V1.02_UHF.c` line 3457, `main()`: `msDelay(2000);    // trying to fix boot up lock`, running right after the buzzer is switched on and BEFORE `LoadSettings(); program_init();` -- i.e. before ANY peripheral is touched, not just the display. **Per explicit user clarification, this is a whole-board peripheral-settle delay** (every peripheral powering up alongside the RT1062 -- display, GPS, nRF, BMS chips -- needs time to complete its own boot before this code starts talking to any of them), not a display-specific fix. The port had no equivalent delay anywhere. Fixed: added `SDK_DelayAtLeastUs(2000000U, SystemCoreClock);` at the very top of `app_init()`, right after `app->last_winbutton = -1;`, before any subsystem init (matching the original's placement before `LoadSettings()`/`program_init()`). Synced to the real project and **verified `-fsyntax-only -Wall -Wextra` clean (exit 0) against the real SDK tree** using the exact compile flags pulled from `Debug/source/subdir.mk`'s pattern rule. **Hardware reflash test still PENDING** -- not yet confirmed the splash-to-MAIN transition actually happens on real hardware.

**ENET hang recurred after reflash with probe reattached (expected -- confirms the diagnosis), fixed with a firmware-side retry so the probe no longer needs unplugging every test cycle, 2026-07-16.** Reflashing necessarily requires the probe attached; the very next boot hung at the identical spot (`lwip_init` -> `netif_add`) with the probe back on, confirming the earlier diagnosis rather than contradicting it. Rather than keep requiring a physical unplug/replug for every test iteration (explicit user request -- "waste of time unplugging it each time"), added real resilience: `PHY_KSZ8081_Init()` (vendor SDK, called inside `netif_add()`) does NOT retry on a genuine MDIO transfer failure, only on a successful-but-wrong-ID read -- so one bad transaction (from probe interference or anything else) fell straight through to lwIP's `LWIP_ASSERT -> for(;;){}` halt, taking the WHOLE BOARD down, not just networking. Fixed in `enet_lwip_rt1062.c`: added `enet_phy_preflight_check()`, a bounded loop (5 attempts) that does a fresh `enet_phy_hw_reset()` + raw MDIO PHY-ID-register read before ever calling `netif_add()`. `enet_lwip_rt1062_init()`'s signature changed `void` -> `int` (0=ok, -1=PHY never responded after all attempts). On -1, ENET/lwIP/TCP are left entirely uninitialized -- **not fatal to boot anymore**: `app_init.c` now tracks this in a new `app->enet_available` flag (`app_context.h`), skips the `tcp_lwip_*_open()` calls when false (previously those calls being skipped wasn't reachable since `enet_lwip_rt1062_init()` never returned failure at all), and `app_loop.c`'s per-iteration TCP/ENET poll block is now gated on the same flag so it never touches an uninitialized netif/listeners. Net effect: even if the debug probe (or anything else) makes the PHY flaky, the board now degrades to "no network this boot" instead of a full silent hang -- display/GPS/BMS/etc all still come up and are testable with the probe left connected. Verified `-fsyntax-only -Wall -Wextra` clean against the real SDK for all four touched files (`enet_lwip_rt1062.c/h`, `app_init.c`, `app_loop.c`, `app_context.h`). **CONFIRMED WORKING on real hardware, 2026-07-16** -- user confirmed this resolved it; probe no longer needs to be unplugged for routine testing.

**Follow-up bug caught on first hardware test, same day: a SECOND unguarded `#if APP_ENABLE_TCP` block.** With the probe attached, the PHY genuinely didn't respond (`enet_init: PHY not responding after 5 reset attempts -- skipping ENET/TCP this boot`, `available=0`) -- exactly the designed-for case -- but `app_init()` still printed `app_init() FAILED -- halting` right after `DISPLAY activate MAIN done`. Cause: `app_init.c` has TWO separate `#if APP_ENABLE_TCP` blocks (a pre-existing structure, not new this session) -- the first (tcp_listener/reset_transport/discovery_transport) was correctly gated on `app->enet_available` when the resilience fix landed; a second, later one (`app->finish_lynx_listener`, was `TCPIPOpenSocket_FinishLynx()`) was missed. Its `tcp_lwip_listener_open()` call still ran unconditionally, failed (no netif exists), and its own `return -1` aborted app_init() entirely -- defeating the whole point of the fix. Fixed: wrapped that block in the same `if (app->enet_available)` check. Verified `-fsyntax-only -Wall -Wextra` clean, synced to the real project. Also exhaustively re-grepped both `app_init.c` and `app_loop.c` for every other `tcp_listener`/`finish_lynx_listener`/`reset_transport`/`discovery_transport` reference -- all others are guarded by their own `client_connected`/`was_connected` runtime flags, which stay false (never set) when the listener was never opened, so they're safe by construction and needed no change.

**Known gap, still not addressed**: `app_genie_dispatch.c`'s `GENIE_DHCP`/`GENIE_BUTTON_SETLAN` touchscreen handlers still call `enet_lwip_rt1062_apply_network_settings()` directly, unguarded by `app->enet_available` -- if ENET failed to init and a user somehow reaches that screen/button, this would operate on a netif that was never added. Lower risk (requires deliberate touchscreen navigation, not hit by normal boot/idle), left unfixed for now to keep this change scoped -- flag if it becomes a real problem.

**`APP_ENABLE_STORAGE` re-enabled, 2026-07-16, per explicit request, now confirmed innocent of the ENET hang.** Flipped 0->1 in `bringup_config.h` (both repos). Current staged flags: Display+TCP+BMS+TIME_SYNC+GPS+STORAGE, everything else (NRF_SPI/UHF/REWIND/GPRS/BOARD_IO) still off. Verified `-fsyntax-only -Wall -Wextra` clean for `app_init.c`+`hello_world.c` together with this combination. **CONFIRMED WORKING on real hardware, 2026-07-16: settings persist correctly across reboots.** User confirmed data survives a reboot -- the app_init.c storage-load-ordering fix (mount/settings-load running first, before anything else touches `app->settings`) is validated on real hardware, not just compile-clean. Storage stage is done.

**Debug-probe/ENET interference CONFIRMED, not just correlated/hypothesized.** User directly confirmed the debug probe clobbers ENET -- upgrades the earlier "leading hypothesis, LinkServer firmware update possibly interfering with JTAG-shared pins" framing to an established fact for this board/probe combination. The bounded PHY-preflight-retry + non-fatal-failure fix (above) is the durable mitigation -- routine testing no longer needs the probe physically unplugged, but be aware ENET/TCP may legitimately be unavailable on any given probe-attached boot, and that's expected, not a new bug.

**GPS boot-to-boot flakiness (satellite tank sometimes fills, sometimes doesn't) -- bounded CFG-message retry tried, then REVERTED, 2026-07-16.** Added `send_cfg_with_retry()` (`neo_m8t_reader.c`), retrying each of the 6 boot-time UBX CFG messages up to 2 times before giving up, on the theory that a well-under-100% single-attempt ACK success rate (measured earlier this session) was causing boot-to-boot inconsistency. **REVERTED per explicit instruction** ("get rid of all the ubx retries. it is slowing things up") -- the retry's own worst-case added latency (each attempt up to 1100ms, x6 messages) was judged not worth it. Back to one attempt per message, matching this function's original (pre-retry-experiment) behavior. Verified `-fsyntax-only -Wall -Wextra` clean after the revert. GPS boot-to-boot flakiness itself remains unresolved -- deprioritized in favor of boot speed.

**GPS signal-check countdown moved earlier in boot, 2026-07-16, per explicit report that the satellite tank now appears noticeably later relative to PPS than on the original hardware.** `app->check_interval_ms`/`check_interval2_ms` (the 2s/15s first-fire delays for battery-status and GPS-signal checks, both confirmed faithful to the original's own `checkInterval`/`checkInterval2` values) were computed at the very END of `app_init()`, after every other driver init (storage, display, BMS, ENET+PHY-retries, GPS's own newly-retried CFG messages) had already run -- on this port's boot, that preceding work can cost several real seconds, so "15s after this line" was actually "15s after boot plus everything else," not "~15s after power-on" like the original's equivalent (same relative code position, but reached almost immediately on the original's much faster Rabbit boot). Moved both lines to right after the whole-board 2-second startup delay, as close to power-on as `app_init()` gets -- schedule values unchanged, just the reference point. Verified `-fsyntax-only -Wall -Wextra` clean. **Not yet hardware-tested.**

**`APP_ENABLE_BOARD_IO` flipped 0->1, 2026-07-16, per explicit request -- next staged subsystem.** Gates `buzzer_rt1062_init()`, `reader_shutdown_rt1062_init()`, `fan_rt1062_init()` -- plain GPIO inits, no shared-bus ordering concerns, each individually confirmed working in earlier standalone sessions, but never yet through the real `app_init()`/`app_loop()` flow alongside everything else now staged. Current staged flags: Display+TCP+BMS+TIME_SYNC+GPS+STORAGE+BOARD_IO; still off: NRF_SPI, UHF, REWIND, GPRS. Verified `-fsyntax-only -Wall -Wextra` clean for `app_init.c`, `app_loop.c`, `app_pc_dispatch.c`, `app_genie_dispatch.c`, `hello_world.c`, and the three driver files themselves. **Not yet hardware-tested.**

**Buzzer boot-chime gap found and fixed, 2026-07-16, per explicit report.** The original (`ACTIVERFID_V1.02_UHF.c` lines 3454-3460) turns the buzzer ON right before the 2-second startup delay and OFF right after `program_init()` returns -- an audible boot chime for the whole startup window, not silent GPIO config. This port's `buzzer_rt1062_init()` only ever configured the pin; nothing called `buzzer_on()` at boot. Fixed: moved `buzzer_rt1062_init()` + added `buzzer_on()` to run BEFORE the startup delay (matching the original's exact order), and added `buzzer_off()` right after the BMS checkpoint -- this port's equivalent of "`program_init()` returns" (STORAGE/DISPLAY/NRF_SPI/TIME_SYNC/BMS correspond to `program_init()`'s content; everything after, ENET/TCP/GPS/etc, corresponds to the rest of `main()`, where the original's buzzer is already off). Verified `-fsyntax-only -Wall -Wextra` clean at both `APP_ENABLE_BOARD_IO` extremes. **Not yet hardware-tested.**

**GPS/GPRS moved to run AFTER display MAIN activation, 2026-07-16, per explicit hard requirement: "main screen needs to be shown in 4 seconds."** `gps_configure_timepulse()`'s 6 UBX CFG messages each carry a no-retry up-to-1100ms timeout (this session's own diagnostics measured well-under-100% single-attempt success), so a bad boot could previously cost ~6.6s in that block ALONE, before `app_init()` ever reached display activation -- blowing the 4-second budget outright. `app_init.c` reordered: GPS config + GPRS init now run AFTER `battery_percent`/`TIME_SYNC read`/`display_activate_form(MAIN)`, not before. This is a DELIBERATE deviation from the original's own ordering (`set_UBX()` before `genieActivateForm(MAIN)`) -- explicit instruction overriding sequence-fidelity for this specific case, not a guess. Confirmed safe to reorder: GPS/GPRS have no data dependency on display activation (the satellite tank is a separately-timed periodic check in `app_loop.c`, not part of the initial MAIN paint). The DS3231 accuracy requirement ("time needs to show exactly what the ds3231 time is") was already satisfied by the existing ordering -- `TIME_SYNC read` (a fresh DS3231 read) still runs immediately before `display_activate_form(MAIN)`, unchanged by this reorder. Verified `-fsyntax-only -Wall -Wextra` clean at both `APP_ENABLE_GPS` extremes. **Not yet hardware-tested against the 4-second requirement.**

**GPS bus-priming experiment added, 2026-07-16, per explicit user prompt.** User pasted the original's own GPS-priming-before-nRF-query quirk (`CS2_ENABLE; msDelay(1); SPIRead(&randomstr,20); CS2_DISABLE; msDelay(200);`, comment "seem to have to do comms with GPS chip on SPI for proper SPI comms to nrf chip") -- on the Rabbit board GPS was always the FIRST SPI user, so only nRF (queried after GPS) ever needed this priming. On this port, with `APP_ENABLE_NRF_SPI` off, GPS's own first UBX message (`ubx_pm2`) IS now the first SPI transaction of the session -- nothing has ever primed the bus before it. Added the identical throwaway-read pattern to `neo_m8t_configure_timepulse()` (`neo_m8t_reader.c`), immediately before `ubx_pm2`, on the theory that whatever made the original pattern necessary (never fully explained, even by the original author) may be a general "first SPI transaction on this hardware is unreliable" phenomenon, not nRF-specific. This is explicitly NOT a confirmed original-source position (the original never needed to prime GPS itself) -- an experimental fix for the measured single-shot reliability gap, not a retry (fixed one-time ~201ms cost, not a per-failure multiplier -- doesn't reintroduce the "slowing things up" concern). Verified `-fsyntax-only -Wall -Wextra` clean. **Not yet hardware-tested -- this is the next thing to check for a first-try ACK improvement.**

**GPS bus-priming experiment: no measurable improvement (still ~1/6 CFG ACKs, still all clean 0xFF on failure), left in place (harmless, fixed ~201ms cost).** Also ruled out this session: SPI mode/baud/pin config (confirmed correct), CFG-PM2 duty-cycle sleep (ruled out via u-blox docs -- CFG-RXM, which actually enables PSM, is never sent). Which single message succeeds is effectively random boot-to-boot, not tied to message position -- points at a marginal timing/signal-integrity issue this environment can't diagnose further (no scope/logic-analyzer access to the physical SPI lines).

**GPS delay CORRECTED to the real confirmed source value, 2026-07-16, after being called out for implementing from a paraphrase instead of checking source first.** First attempt (a `systick_ms_now()`-based "at least 2000ms since boot" gate) was invented directly from the user's verbal instruction without re-reading `ACTIVERFID_V1.02_UHF.c` -- user: "You need to always read ActiveRFID_V1.02_UHF.C to see exact timing of when things are brought up. This is a mantra you MUST follow." Went back to the source and found the REAL mechanism: line 3479, `main()`: `msDelay(2500);` -- a discrete, position-anchored 2.5-second delay (not a since-boot calculation), sitting right after the nRF-fw-version splash/contrast write and BEFORE fan-off, DS3231 ISR install, `TCPIPOpenSockets()`, `httpc_init()`, and finally `set_UBX()` (line 3504). Replaced the invented gate with `SDK_DelayAtLeastUs(2500000U, SystemCoreClock);` immediately before `gps_configure_timepulse()` -- the confirmed value (2500ms, not 2000ms) at the position closest to the original's actual intent. Does not replicate the OTHER work in that exact original window (nRF fw-version query, fan on/off, TCP socket opens) -- those either don't apply currently (NRF_SPI off) or already happened earlier in this port's boot order; only the confirmed delay value itself was ported. No effect on the "MAIN in 4 seconds" requirement -- GPS still runs after display activation. Verified `-fsyntax-only -Wall -Wextra` clean. **Not yet hardware-tested.**

**Likely real root cause found for GPS single-shot UBX reliability, 2026-07-16: missing SPI pad drive-strength/slew-rate configuration.** After exhausting protocol/timing-level investigation (byte-for-byte confirmed identical to the original's `process_UBX()`, including checksum computation and CS-to-clock timing margin), and after the user directly confirmed via live Dynamic C debugging that the original DOES get a clean, immediate first-try ACK (ruling out "was never reliable" as an explanation) -- checked `board/pin_mux.c` for the LPSPI3 bus pins and found NONE of them (SCK, SDI, SDO, or either CS pin -- GPS's own and nRF's) had ever had an explicit `IOMUXC_SetPinConfig()` call. Only `IOMUXC_SetPinMux()` (function selection) existed; pad drive-strength/slew-rate/speed were left at the silicon's power-on-reset default the whole time. A weak/marginal output driver on SCK/MOSI/CS trying to drive real PCB trace + the GPS module's input capacitance would produce a degraded signal that sometimes crosses the logic threshold cleanly and sometimes doesn't -- consistent with the observed pattern (genuinely random which message succeeds, clean `0xFF` on failure rather than corrupted data, i.e. a signal too weak to reliably sample rather than a protocol error). Fixed via the MCUXpresso Pins tool (safer than a manual text edit, since `pin_mux.c` is Config-Tools-generated and warns manual edits get overwritten) -- user set explicit pad config for all 5 pins: strong drive strength (`DSE=7`, max, on 4 of 5 pins), fast slew rate, and hysteresis enabled on the input (SDI/MISO). Verified `-fsyntax-only -Wall -Wextra` clean. **CONFIRMED WORKING on real hardware, 2026-07-16 -- root cause resolved.** User reflashed and confirmed 5/5 CFG messages ACKed cleanly on the first try (`pm2`, `navx5`, `nav5`, `tp5`, `gnss` all `ACKed`, no timeouts) -- a complete turnaround from the ~1/6 rate measured throughout this entire investigation. The missing pad drive-strength/slew-rate config on the LPSPI3 bus pins (SCK/SDI/SDO/both CS pins) was the real, confirmed root cause of GPS's single-shot UBX unreliability -- not bus contention (real but insufficient fix), not timing, not protocol logic, not GPS module power-save state. This closes out the single-shot-reliability investigation that spanned most of this session.

**GPS PVT poll still failing, separate/lower-priority, unresolved.** `neo_m8t_poll_pvt()` consistently returns all-`0xFF` replies with `offset=-1` on every attempt (module not replying meaningfully to UBX-NAV-PVT polls), most recently also with `pps=0` (earlier in the session PPS was confirmed toggling, so this may be a sky-view/antenna issue as much as a code issue). Deprioritized this session behind the ENET/display work; resume separately, see [[project_gps_spi_bringup]] for the tracing done so far (byte-for-byte UBX protocol confirmed correct, diagnostic tracing added and restricted to PVT-only after an earlier self-inflicted splash-hang regression).

Two real bugs found and fixed getting the first flash to boot: (1) a
regression introduced the same session -- gating `lpi2c1_bus_rt1062_init()`
behind `APP_ENABLE_TIME_SYNC || APP_ENABLE_BMS` caused an LPI2C1
interrupt storm, since `peripherals.c`'s Config-Tools-generated
`LPI2C1_init()` arms that interrupt unconditionally regardless of any
flag -- reverted to unconditional; (2) a pre-existing unresolved
placeholder, `display_activate_form(0)` (form 0 = splash screen, not
MAIN), left over from before `app_init()` had ever actually run on
hardware -- fixed to the real original sequence,
`genieActivateForm(GENIE_FORM_MAIN); updateGenie_Main();`.

## Session wrap-up, 2026-07-16 (end of session -- read this first for orientation)

Very long session, full arc is in the detailed entries above (search for
each topic if you need the reasoning); this is the TL;DR for picking up
cold. Next planned work: **UHF reader comms** (`APP_ENABLE_UHF`, SIM7200
over LPUART8) -- not yet resumed this session, was individually confirmed
working in an earlier standalone session (antenna detection, tag-read
time-sync gate) but never yet run through the real `app_init()`/
`app_loop()` flow alongside everything else now staged.

**Current `bringup_config.h` staged flags**: `APP_ENABLE_DISPLAY=1`,
`APP_ENABLE_TCP=1`, `APP_ENABLE_BMS=1`, `APP_ENABLE_TIME_SYNC=1`,
`APP_ENABLE_GPS=1`, `APP_ENABLE_STORAGE=1`, `APP_ENABLE_BOARD_IO=1`.
Still off: `NRF_SPI`, `UHF`, `REWIND`, `GPRS`.

**CONFIRMED WORKING on real hardware, all together**: full boot to
`COMPLETE, entering main loop` within a few seconds; ENET link-up + real
DHCP lease; MAIN form shows reliably (not stuck on splash); settings
persist across reboots; buzzer sounds as a boot chime; GPS PPS toggling +
full satellite tank + correct DS3231-synced date/time; **GPS CFG-message
single-shot reliability now 5/6 clean first-try ACKs** (up from ~1/6 for
most of this session -- see below); DS3231 time-sync-to-PPS mechanism
verified line-for-line against the original's `SetTime_uBlox()`.

**Three major root causes found and fixed this session**:
1. **ENET/MDIO hang** -- the debug probe (LinkServer) interferes with
   GPIO1 pins 9/10 (ENET_RST/ENET_INT, shared with JTAG_TDI/TDO) during
   `enet_phy_hw_reset()` -- CONFIRMED by the user directly, not just
   correlated. Mitigated (no probe-level fix found -- see
   `enet_lwip_rt1062.h`) with a bounded PHY-presence preflight retry +
   non-fatal fallback (`app->enet_available`) so a probe-caused glitch
   degrades to "no network this boot" instead of hanging the whole board.
2. **Genie display stuck on splash** -- the confirmed original
   `msDelay(2000)` boot delay (whole-board peripheral settle time, not
   display-specific) was entirely missing, combined with `app_init()`'s
   one-shot `display_activate_form(MAIN)` call silently no-oping if the
   display hadn't been detected yet at that exact moment. Fixed with the
   delay (confirmed-sourced) plus an ACK-checked retry in the main loop
   (`app->display_main_shown`, `display_is_online()`) that keeps trying
   until the command actually lands, not just until the display is
   detected.
3. **GPS single-shot UBX unreliability** (the single longest thread this
   session) -- root cause was `board/pin_mux.c` never having an
   `IOMUXC_SetPinConfig()` call for ANY of the LPSPI3 bus pins (SCK, SDI,
   SDO) or either chip-select (GPS's own, nRF's) -- only
   `IOMUXC_SetPinMux()` (function selection) existed, so pad
   drive-strength/slew-rate were left at the silicon's power-on-reset
   default the entire time this project has existed. A weak/marginal
   output driver produces a signal that sometimes crosses the logic
   threshold cleanly and sometimes doesn't -- matches the observed
   pattern exactly (random which message succeeds, clean `0xFF` on
   failure = signal too weak to sample, not corrupted data). CONFIRMED
   via live Dynamic C debugging that the original hardware gets a clean
   first-try ACK, ruling out "was never reliable" before finding this.
   Fixed via the MCUXpresso Pins tool (max drive strength + fast slew on
   4 of 5 pins, hysteresis on the input) -- confirmed 5/6 CFG messages
   now ACK cleanly on the first try. See [[project_gps_spi_bringup]] (not
   this repo) for the full investigation arc and the general lesson
   (`IOMUXC_SetPinMux()` alone doesn't set electrical drive
   characteristics -- check for a missing `SetPinConfig()` before
   assuming a protocol/timing bug when a signal-level symptom looks
   intermittent/probabilistic rather than deterministically broken).

**Two smaller boot-order fixes, per explicit hard requirements**:
- MAIN screen shown within 4 seconds of boot -- addressed by moving GPS
  config and GPRS init to run AFTER display activation instead of
  before (a deliberate, instructed deviation from the original's exact
  ordering, since GPS's CFG messages have no retry and could previously
  cost real time before the display ever got a chance to activate).
- GPS UBX messages delayed at least 2.5s after boot -- CORRECTED after
  initially being implemented as an invented "since-boot" gate instead
  of checking source first (explicit user correction: "you need to
  always read ActiveRFID_V1.02_UHF.C... this is a mantra you MUST
  follow"). Real value: `msDelay(2500)` at `ACTIVERFID_V1.02_UHF.c` line
  3479, a discrete position-anchored delay, not a since-boot
  calculation -- now implemented as `SDK_DelayAtLeastUs(2500000U, ...)`
  immediately before `gps_configure_timepulse()`.

**Still open**: `ubx_prt` (CFG-PRT, the 6th/last CFG message) still
times out even with the pad-config fix -- the one holdout out of 6, a
plausible special case since it's the only message that reconfigures the
SPI port's own interface behavior (protocol mask, SPI mode) mid-session.
Deprioritized per explicit user call ("ok we will now move to reader
coms") -- revisit if it becomes a real problem, otherwise 5/6 was judged
acceptable for now. Also still open, unrelated: GPS PVT poll (NAV-PVT,
not the CFG messages) reliability during actual operation hasn't been
separately re-measured since the pad-config fix -- likely also improved
given it uses the same electrically-fixed SPI lines, worth confirming
whenever GPS work resumes.

**Diagnostics still in place, not yet cleaned up** (all clearly marked
`TEMPORARY DIAGNOSTIC` in source): boot-progress checkpoints throughout
`app_init.c`/`enet_lwip_rt1062.c`, the Genie debugger-handler wiring in
`display_stub.c`, the UBX CFG ACKed/FAILED prints and timeout hex/ascii
dump in `neo_m8t_reader.c`, the seconds-digit ACK-failure print in
`app_loop.c`. Leave these in for now -- cheap, and still useful if any of
the above regress.

**Note on `board/pin_mux.c`**: Config-Tools-generated, lives only in the
real MCUXpresso project (not synced from this repo's `firmware_source/`).
The pad-config fix above was applied through the Pins tool GUI, not a
manual text edit (a manual edit attempt hit a persistent Windows
file-lock -- the file was open somewhere holding an exclusive lock even
after closing visible editor tabs; using the GUI tool sidestepped it
entirely and is also the SAFER way to change this specific file, since
its own header warns manual edits get overwritten on next regeneration).

## Open items (unresolved)

- `TRIGGER` pin (GPIO1 pin 23): primitive built (`trigger_input_read()`),
  behavior unknown -- no source for what `Settings.TriggerOn` does, or
  whether it needs edge detection vs polling.
- `USB_STATUS` (GPIO4 pin 0), `USB_DTR` (GPIO2 pin 16): no drivers
  built, purposes are guesses from pin names only.
- `BUTTON_LED`: RESOLVED 2026-07-17 -- `button_led_rt1062.c/h` built and
  wired up (RED on boot, solid GREEN once time sync is first achieved,
  GREEN/off 1Hz blink while reading -- a deliberate test-only red/green
  split, see project memory for the confirmed source lines and reasoning).
  Turned out to be a genuine BIDIRECTIONAL (2-lead antiparallel) LED,
  driven by TWO GPIOs together, not one pin against a fixed ground. Pin
  1 identity went through FIVE rounds of correction:
    1. Initial guess: "pin 1" = an unconfirmed GPIO3 pin 4, via raw
       `fsl_gpio.h` with no `pin_mux.c` check -- abandoned after "not
       seeing led lit" (the real problem was an unrelated pin-2 issue at
       the time, not this pin -- but see round 3, this exact pin/bank
       later turned out to be the WRONG one, distinct from round 4's
       GPIO3-pin-4 dead end on a different bank).
    2. Second guess: "pin 1" = MODEM_PWR (GPIO1 pin 22,
       `BOARD_INITPINS_MODEM_PWR_handle`), repurposed since modem power
       is hardwired always-on externally on this board
       (`gprs_transport_rt1062.c`'s `rt1062_gprs_set_power_enable()`
       made a no-op for the same reason) -- also abandoned; LED still
       didn't light, and debug tracing confirmed the software was
       correctly calling this pin, ruling out a software bug.
    3. From the schematic photo the user provided ("Led pins are
       GPIO_SD_B0_04-SD1_D2 and GPIO_SD_B0_05-SD1_D3"): "pin 1" =
       GPIO_SD_B0_04, inferred as GPIO3 pin 16 by pattern-matching its
       two SD_B0-bank neighbors -- **the correct pin, but implemented
       incompletely**: only `GPIO_PinInit()` (raw `fsl_gpio.h`) was
       called, which never touches the pad's IOMUXC mux-select. User
       reported "led pin only showing 1.2V" (a floating voltage, not a
       real logic-level swing) -- misdiagnosed at the time as a wrong
       pin rather than a missing mux call.
    4. Mistaken pivot to `GPIO3 pin 4` via `GPIO_SD_B1_04` (**SD_B1**
       bank, a different pin from round 1's SD_B0-bank guess), chasing a
       pre-existing `identifier: BUTTON_LED` label already sitting in
       `board/pin_mux.c` at that exact pin, complete with a real
       generated `IOMUXC_SetPinMux`/`SetPinConfig`/`GPIO_PinInit`.
       Reported "no change" -- this `pin_mux.c` label turned out to be
       stale/unrelated to the real LED, not authoritative after all.
    5. **CONFIRMED 2026-07-17, final**: the user directly clarified the
       schematic reading -- "GPIO_SD_B0_05-SD1_D3 is one pin" -- meaning
       each hyphen-joined string in the photo names ONE pin by its two
       alternate names (matching `pin_mux.c`'s own label format,
       `SD1_D3/J22[2]` for that exact READER_PWR pin), NOT two separate
       pins. This confirms round 3's schematic reading (`GPIO_SD_B0_04`
       = GPIO3 pin 16) was right all along. Independently cross-checked
       against `drivers/fsl_iomuxc.h`'s own
       `IOMUXC_GPIO_SD_B0_04_GPIO3_IO16` macro (ALT5) -- confirms the
       pin number, not a guess. Root-caused round 3's 1.2V symptom for
       real this time: `GPIO_SD_B0_04` has NO entry anywhere in
       `pin_mux.c` (unlike READER_PWR/READER_SHUTDOWN, which do, despite
       using the same raw-`fsl_gpio.h` style), so the pad stayed on its
       power-on-reset ALT0 default (`USDHC1_DATA2`, confirmed via the
       same `fsl_iomuxc.h` macro list) the whole time -- GPIO3 writes
       had zero effect on the physical pin. Same quirk CLASS as the GPS
       SPI pad-drive-strength bug (missing `IOMUXC_Set*` config), see
       project memory `project_gps_spi_bringup`, but this time the
       mux-select itself was missing, not just pad drive-strength.
       **Fix**: `button_led_rt1062_init()` now explicitly calls
       `IOMUXC_SetPinMux(IOMUXC_GPIO_SD_B0_04_GPIO3_IO16, 0U)` +
       `IOMUXC_SetPinConfig(..., 0xB0U)` itself (new
       `#include "fsl_iomuxc.h"`) before `GPIO_PinInit()`, since this pin
       has no `BOARD_InitPins()` support to lean on. "pin 2" = READER_PWR
       (GPIO3 pin 17, GPIO_SD_B0_05) unchanged throughout every round --
       never in question.
  `button_led_red()`/`button_led_green()`/`button_led_off()` drive both
  pins together as a complementary pair. A FUTURE board needing real
  reader power control on READER_PWR would need that no-op undone AND
  the LED's pin 2 moved elsewhere (pin 1/GPIO3-16 was never shared with
  anything else, no equivalent concern there). Which HIGH/LOW
  combination actually produces which color on the real board is still
  an inference, not independently confirmed -- flag/swap if it comes up
  inverted or backwards. Code fixed, synced to both repos, and verified
  via `-fsyntax-only`; NOT YET hardware-tested with the corrected pin 1
  (which now includes the previously-missing mux call; debug tracing
  left in place to confirm the calls fire and what levels they write on
  the next flash). **Lesson for future pin-identity questions in this
  project**: neither a schematic photo's text, a neighboring-pin
  pattern-match, nor a `pin_mux.c` Pins-list label alone is fully
  trustworthy on its own -- cross-check the candidate pin against ALL
  THREE (the schematic, `drivers/fsl_iomuxc.h`'s mux-macro list for the
  real pin-number mapping, and `pin_mux.c`'s actual generated
  `IOMUXC_SetPinMux()`/`SetPinConfig()` calls for whether the mux is
  genuinely being set) before committing to a raw-`fsl_gpio.h` pin that
  isn't part of the generated Pins tool -- and if it isn't part of that
  tool, this driver must set the mux itself; `GPIO_PinInit()` alone is
  never sufficient for a pin `pin_mux.c` has never touched.
  **PAUSED/DEPRIORITIZED 2026-07-17, per explicit instruction ("move
  on") -- still NOT actually working.** Pin identity (GPIO3 pin 16 +
  READER_PWR) is schematic-confirmed correct and not in question. Drive
  strength was bumped to this project's own proven max (`0x1079U`, same
  as `SPI_CS`/`SPI_SDO`/`LPSPI3_SCK`) -- still reads ~1.2V. An exhaustive
  project-wide grep confirmed NOTHING else in software touches GPIO3 pin
  16 or 17 (no mux conflicts, no raw register pokes in range, no unused-
  peripheral driver actually initialized) -- so this is now believed to
  be a physical/board-level issue (populated pull resistor, short, or
  broken trace on this SD_B0-group net) outside what firmware can fix.
  If revisited: do NOT re-litigate pin identity -- go straight to a
  power-off resistance/continuity check from pin 1 to GND/3.3V, and
  compare voltage at the MCU pin itself vs. at the LED's leg.
- `MODEM_DTR` boot-level conflict: original source sets it LOW at boot
  ("wakeup"), generated `peripherals.c` initializes it HIGH. Unresolved.
- `LANSERVER_CONN_TIMEOUT_MS`, `board_vers` pre-detection default,
  MAX17303 register `0x21`'s real datasheet name, UHF start/stop's
  exact display form/LED reset behavior: all flagged defaults, never
  confirmed.
- Network IP/DHCP reconfiguration from the touchscreen (was
  `UpdateRabbitIP()`) resolved 2026-07-16 -- see `enet_lwip_rt1062.h`'s
  `enet_lwip_rt1062_apply_network_settings()`/`_poll_dhcp_fallback()`
  (faithful port including the confirmed hardcoded 255.255.0.0 netmask
  and the 8-second DHCP-timeout-then-fallback-to-static behavior,
  tracked explicitly since lwIP's `dhcp_start()` has no built-in
  timeout/fallback unlike Rabbit's `ifconfig()`), wired from both real
  call sites in `app_genie_dispatch.c` (the `GENIE_DHCP` toggle and the
  `GENIE_BUTTON_SETLAN` keypad-entry handler). Deliberately NOT wired:
  DNS server (`rabbit_dns`) -- `LWIP_DNS` is off in this project's
  `lwipopts.h`, and nothing in this port resolves hostnames yet (every
  connection target is already a raw IP -- see `ip_addr_parse.h`).
  Firmware check/download/install (was `check_fw()`/`install_firmware()`)
  is still stubbed in `app_genie_dispatch.c` -- needs gluing the
  existing `fw_version_check.h`/`fw_downloader.h`/`fw_install_mcuboot.h`
  pure-logic modules to a real HTTP transport, a larger OTA-path task
  covered separately in `OTA_MCUBOOT_INTEGRATION.md`.
- Both stubs formerly in `GENIE_REMOTE`'s handler are now resolved
  (2026-07-16). MAX17303 FAULTS-clear write: `max17303_fuel_gauge_rt1062.h`
  now defines `MAX17303_REG_FAULTS` (0xAF, MAIN address -- corrected
  out of its offset-based NV-group guess once the one real call site
  in `ACTIVERFID_V1.02_UHF.c` confirmed it), wired into
  `app_genie_dispatch.c`'s `GENIE_REMOTE` case behind `APP_ENABLE_BMS`
  (a deliberate safety-consistency choice, not a hard technical
  need -- see that call site's own comment). `Toggle_Modem()`: user
  pasted the real body; turned out `gprs_modem_toggle()`
  (`gprs_modem.c`, built in an earlier session before this source was
  available) already matched it faithfully -- just needed wiring up.
  Now called from `GENIE_REMOTE`'s off-branch, gated `APP_ENABLE_GPRS`
  (its `app->modem.transport` is only set by `gprs_modem_init()` in
  that same flag's `app_init.c` block -- calling it with GPRS off
  would be a NULL transport dereference). The pasted source's
  `genieWriteObject(GENIE_OBJ_TANK, GENIE_TANK_4G, 0)` (a display call,
  correctly kept out of `gprs_modem.c`'s transport-only layer) is
  called directly alongside it, gated `APP_ENABLE_DISPLAY`. Not
  ported: a trailing `printf(buf,"\n\r")` in the original that passes
  `buf` (still holding the last AT command) as a format string -- a
  debug-leftover bug in the original, not real device I/O.
- littlefs and lwIP source/drivers ARE now present in the real
  MCUXpresso project (copied in alongside the rest of `firmware_source/`
  for the `app_init()` integration above) -- but `APP_ENABLE_STORAGE`/
  `APP_ENABLE_TCP` are currently off as part of the Genie-only staging
  above, not because either is broken; both were separately confirmed
  working in earlier standalone bring-up sessions.

## Session update, 2026-07-17: UHF reader comms staged

**`APP_ENABLE_UHF` flipped 0->1** -- next staged subsystem per the prior
session's own plan. Individually confirmed working in an earlier
standalone session (antenna detection via DC+return-loss check,
tag-read time-sync gate, `uhf_reader_start()` call site), but never
run through the real `app_init()`/`app_loop()` integrated flow until
now.

**Found and fixed a real gap before flipping the flag**: `app->uhf_transport`
was never populated anywhere. Every other transport in this port
follows the same pattern -- `nrf_spi_transport_rt1062_init()` is
called once in `app_init.c` to wire up the function-pointer struct --
but the UHF equivalent, `uhf_transport_rt1062_init()`, had no call
site at all. `app_pc_dispatch.c`'s `app_uhf_reader_control()` calls
`uhf_reader_open(&app->uhf, &app->uhf_transport)`, which does
`t->open(t->ctx, UHF_UART_BAUD)` with no NULL check -- so the very
first `UHF_START` (touchscreen, PC command, or the abnormal-shutdown
auto-resume check already in `app_init.c`) would have called through
a NULL function pointer and hard-faulted, since the struct was still
sitting at its `memset()` zero value. Fixed by adding
`app->uhf_transport = uhf_transport_rt1062_init();` in `app_init.c`,
gated `#if APP_ENABLE_UHF`, right before the existing abnormal-shutdown-
resume check. Confirmed `uhf_transport_rt1062_init()` itself only
wires up function pointers and touches no hardware (the real
`LPUART_Init()` call is inside `rt1062_uhf_open()`, invoked later by
`uhf_reader_open()` each time reading actually starts) -- so this is
safe to call unconditionally at boot, matching the original's own
`Open_Reader()`-called-on-every-start design rather than pre-opening
the port here.

Verified `-fsyntax-only -Wall -Wextra` clean (exit 0, zero warnings)
against the real SDK tree, using the exact flags from
`Debug/source/subdir.mk`, for `app_init.c`, `app_loop.c`,
`app_pc_dispatch.c`, `app_genie_dispatch.c`, `uhf_reader.c`, and
`uhf_transport_rt1062.c`. Also re-verified the `APP_ENABLE_UHF=0`
extreme still compiles clean. Both repos synced and diff-confirmed
identical.

**Current staged flags**: `APP_ENABLE_DISPLAY=1`, `APP_ENABLE_TCP=1`,
`APP_ENABLE_BMS=1`, `APP_ENABLE_TIME_SYNC=1`, `APP_ENABLE_GPS=1`,
`APP_ENABLE_STORAGE=1`, `APP_ENABLE_BOARD_IO=1`, `APP_ENABLE_UHF=1`.
Still off: `NRF_SPI`, `REWIND`, `GPRS`.

**Not yet hardware-tested** -- no toolchain in this environment to
produce a flashable binary, only `-fsyntax-only`. Next step: build/
flash in MCUXpresso IDE, then confirm `UHF_START` (touchscreen or PC
command) brings the reader up without crashing, antenna detection
still reports correctly through the integrated flow (not just the
retired `hello_world.c` harness it was last confirmed against), and a
real tag read flows through `uhf_event_cb()` -> the time-sync gate ->
littlefs logging end to end.

## Session update, 2026-07-17 continued: `app_uhf_reader_control()` fidelity fixes

User flashed and reported no double-beep on starting reading (buzzer
hardware itself confirmed fine -- boot chime audible). Re-pulled the
real `UHF_Reader_Control()` (`ACTIVERFID_V1.02_UHF.c` lines 1503-1549)
and `StartReaders()`/`StopReaders()` (`UHF_READER.LIB` lines 890-943)
and found `app_uhf_reader_control()` (`app_pc_dispatch.c`) had drifted
from source in three ways, all fixed:

1. **Beep mechanism was wrong.** Both the start double-beep and stop
   single-beep were wired through `app_beep_n()`, which (a) is gated on
   `app->settings.beeper` -- but the original's raw
   `BitWrPortI(PBDR,...,2); msDelay(50);` toggle here is completely
   unconditional (confirmed: every OTHER `Beep()` call site in the
   original has its caller check `Settings.Beeper` first; this one
   doesn't), and (b) queues a non-blocking pulse train advanced by
   `process_beeper_and_dim()` on later loop ticks, while the original
   blocks synchronously right there. Added `buzzer_beep_n_blocking(int
   count)` to `buzzer_rt1062.c/h` -- a direct, unconditional, blocking
   `buzzer_on()`/50ms/`buzzer_off()`/... loop matching the raw toggle
   exactly -- and switched both call sites to it.
2. **Beep position was wrong** -- was at the very top of the enable
   branch; the source fires it after `Open_Reader()`/
   `TM_InitialiseReader()`/the nand-size check, immediately before
   `StartReaders()`. Moved to match.
3. **`fan_on()` was unconditional at the top of the enable branch --
   a real, separate bug.** `StartReaders()`'s source shows the fan
   only switches on INSIDE the `if(ants)` branch, right after the
   start command is actually sent; the `else` (no antenna) branch
   aborts with no fan-on at all. The port would have spun the fan up
   on every start attempt regardless of antenna presence. Fixed by
   moving `fan_on()` into the existing `if (app->uhf.ants == 0) {...}
   else {fan_on(); ...}` check -- the same antennas-present condition
   `StartReaders()` itself gates on.

`uhf_reader_start()`/`uhf_reader_stop()` (`uhf_reader.c`) needed no
changes -- both already correctly document fan-on/off as the caller's
responsibility; the bug was entirely in the caller. Verified
`-fsyntax-only -Wall -Wextra` clean against the real SDK for
`app_pc_dispatch.c`, `buzzer_rt1062.c`, `app_init.c`, `app_loop.c`,
`app_genie_dispatch.c`, at both `APP_ENABLE_UHF` extremes. Both repos
synced.

## Session update, 2026-07-17 continued: beep timing, UHF debug tracing, log cleanup

Double-beep confirmed working after the fix above, but user reported
three more things: beep should fire immediately on button press (not
after reader-open/initialise), the reader itself fails its startup
init and needs debug visibility, and the LPUART5 console has too much
boot-time noise. Three changes:

1. **Beep moved to the very top of `app_uhf_reader_control()`'s enable
   branch**, before `uhf_reader_open()`/`uhf_reader_initialise()`, per
   explicit instruction. This is a DELIBERATE deviation from the real
   source (which fires it after reader-open/initialise/nand-check) --
   documented in-code as instructed, not a fidelity guess.
2. **Per-step debug tracing added to `uhf_reader.c`**, same
   `debug_printf`-over-LPUART5 pattern already used in
   `neo_m8t_reader.c` for the GPS investigation. `send_and_wait()` now
   takes a step-name label and logs write/read failures and reply byte
   counts; every command in `uhf_reader_open()`,
   `uhf_reader_set_antennae()` (plus explicit dc_mask/rl_mask/final-ants
   prints), `uhf_reader_initialise()`'s full config sequence,
   `uhf_reader_start()`, `uhf_reader_stop()`, and
   `uhf_reader_get_temperature()` now traces pass/fail -- should pin
   down exactly which command in the SIM7200 config sequence stops
   getting a reply.
3. **Boot-time LPUART5 checkpoint spam removed**: the entire
   `app_init.c` boot-progress-checkpoint series (~13 lines, added
   2026-07-16 to bracket the ENET/splash hangs -- both long since
   root-caused and fixed, outliving their own stated removal criterion)
   and the matching `enet_lwip_rt1062_init()` checkpoint series (~8
   lines) are gone. Kept: a single final `app_init: COMPLETE` line, the
   PHY-not-responding warning, and all ongoing-operation PRINTFs (GPS
   signal, Genie events, DHCP lease, etc -- untouched).

**Also fixed a real, pre-existing, unrelated host_tests gap found while
adding the UHF tracing**: `neo_m8t_reader.c` already used
`debug_printf` (since 2026-07-16) and is part of `test_neo_m8t_reader`
in `host_tests/Makefile`, but no stub/implementation was ever in that
test's dependency list -- `make` would fail to LINK it (undefined
`debug_printf`), never caught since verification here has only ever
been `-fsyntax-only` (no native gcc in this environment to actually
link). Adding the same pattern to `uhf_reader.c` would have compounded
the gap for `test_uhf_reader`. Fixed: added
`host_tests/test_stubs/debug_console_stub.c` (trivial
`vprintf`-forwarding stub) and a new per-test `_EXTRA` Makefile
variable wired into `BUILD_RULE` (backward compatible). **Not verified
by an actual `make` run** -- please run `cd host_tests && make` to
confirm the whole suite is still green.

Verified `-fsyntax-only -Wall -Wextra` clean against the real SDK for
`app_pc_dispatch.c`, `uhf_reader.c`, `app_init.c`,
`enet_lwip_rt1062.c`, `app_loop.c`, `app_genie_dispatch.c`, at both
`APP_ENABLE_UHF` extremes. Both repos synced.

## Session update, 2026-07-17 continued: root cause of "reader is not reading" found

The debug tracing above paid off immediately. User's LPUART5 log
showed EVERY command in the UHF init sequence getting a clean "OK, 0
reply bytes" (not corrupted, not a timeout -- just uniformly nothing
back), ending in `final ants=0x00`. That pattern pointed at the reader
never being powered on, not a protocol bug.

Traced `reader_power_set()` (`reader_shutdown_rt1062.c`, GPIO3 pins
15/17): its ONLY call site anywhere in this port was
`app_uhf_active_mode_toggle()` (`app_pc_dispatch.c`'s `GENIE_SYSTEM`
touchscreen handler) -- fires only on a LIVE toggle event.
`reader_shutdown_rt1062_init()` leaves both pins reader-OFF as a "safe
default" at boot, by design, deferring the real on/off to
`reader_power_set()` -- which nothing else ever called at boot.

Found the missing piece in the real source:
`ACTIVERFID_V1.02_UHF.c` lines 2576-2582, the LAST statement inside
`program_init()` -- runs on EVERY boot, unconditionally:
```c
if(Settings.System){
    BitWrPortI(PBDR,&PBDRShadow,0,4);    // reader on
    BitWrPortI(PADR,&PADRShadow,1,0);    // reader power on
}else{
    BitWrPortI(PBDR,&PBDRShadow,1,4);    // reader off
    BitWrPortI(PADR,&PADRShadow,0,0);    // reader power off
}
```
Exactly `reader_power_set(Settings.System)` -- this block was NEVER
PORTED. Since `Settings.System` persists across reboots
(`APP_ENABLE_STORAGE=1`), a device already configured as a UHF station
would have the reader powered OFF every boot after the first, unless
the touchscreen System toggle happened to be flipped again that same
session. Explains "fails startup init" exactly.

**Fix**: added `reader_power_set(app->settings.system);` to
`app_init.c`, gated `#if APP_ENABLE_BOARD_IO`, right before
`buzzer_off()` (this port's own "end of program_init()-equivalent
work" marker) -- matches the original's position (last statement of
`program_init()`, after settings are loaded) as closely as this port's
boot order allows. Verified `-fsyntax-only -Wall -Wextra` clean at
both `APP_ENABLE_BOARD_IO` extremes. Both repos synced. **Not yet
hardware-tested -- expected to be the actual fix.** Next flash should
show real (non-zero) reply bytes throughout the UHF trace.

**Reader power dropped to 20dBm and a real display gap fixed, same day.** User reported the reader now starts and reads chips, but the board fully crashes/resets right after (a beep, then reset) -- and asked "you are using power of 20dBm right?" Checked: no, `uhf_reader_set_antennae()`'s power-set command was hardcoded to the original's real `UHF_POWER_31_5DBM` default; `UHF_POWER_20DBM` existed in `uhf_commands.h` (added 2026-07-14, doc comment: "for a power-limited test board that can't supply the original's default 31.5dBm") but was never wired into the current integrated code, only the retired standalone harness. Switched to `UHF_POWER_20DBM` -- a brownout from peak RF-transmit current draw on this same power-limited bench is a strong candidate for "starts, beeps on a read, then crash/reset." Reviewed (no changes needed) the two most-likely software crash candidates given this is the first time real tag-read data has ever flowed through the actual integrated loop -- `uhf_packet_parser.c` and `uhf_chip_array.c` are both properly bounds-checked. No custom `HardFault_Handler` exists in this port; if the power fix doesn't resolve it, next step is a register dump at the crash, not more code review.

Separately, user asked "are you updating the screen with the latest chip read?" -- answer was no, a real gap: `TM_ProcessChip()`'s tail (`UHF_READER.LIB`) writes each newly-read (and CHANGED) chip code to `GENIE_TXPDR_STR`, formatted per `Settings.OutputType` (DEC/HEX) -- never ported. Added `chipcode_display` to `app_context_t` and the change-detected `display_set_string(GENIE_TXPDR_STR, ...)` write to `uhf_event_cb()`'s `UHF_FRAME_TAG_READ` case in `app_loop.c`, gated `APP_ENABLE_DISPLAY`. Deliberately NOT ported: `TM_ProcessChip()`'s separate `diag_visible`-gated per-antenna RSSI gauge write (diagnostics-screen-only, out of scope for what was asked).

Verified `-fsyntax-only -Wall -Wextra` clean for all touched files at the relevant flag extremes. Both repos synced. **Not yet hardware-tested.**

**"Reading stalls after ~10s" root cause found and fixed -- a real, generalizable LPUART bug affecting all four interrupt-driven RX drivers in this port.** Tracing (per-poll byte counts, nand_log flush return codes) showed total silence starting immediately after a successful flash write, with a bigger flush (19 records) more reliably triggering it than smaller ones, and confirmed via explicit user report ("not a board freeze - break of reading loop") that the app itself never called stop.

Root cause: `uhf_transport_rt1062.c`'s `LPUART8_IRQHandler()` only ever checked/cleared `kLPUART_RxDataRegFullFlag` (RDRF), never `kLPUART_RxOverrunFlag` (OR). Per the LPUART peripheral's documented behavior (confirmed in the real SDK's `fsl_lpuart.h`), once OR sets, the receiver stops transferring new data into the readable register -- RDRF can never assert again until OR is explicitly cleared (it's not hardware-auto-cleared). Since RDRF was the ISR's only enabled interrupt source, an overrun was a **permanent, self-sustaining deadlock**. `mflash_drv.c` disables global interrupts for the duration of any flash program/erase (required for XIP flash); a bigger `nand_log` flush is more likely to need an actual block erase, a much longer window, during which LPUART8's 1-byte-deep hardware receive register overflows almost immediately if reader traffic is still arriving -- exactly matching the observed correlation.

Fixed two-part (both required): the ISR now checks/clears `kLPUART_RxOverrunFlag` before draining RDRF; `rt1062_uhf_open()` now also enables `kLPUART_RxOverrunInterruptEnable` (without it, an overrun could never re-trigger the ISR to clear itself). **Generalized to all four LPUART drivers in this port** (`uhf_transport_rt1062.c`/LPUART8, `lpuart5_console_rt1062.c`/LPUART5, `genie_transport_rt1062.c`/LPUART2, `gprs_transport_rt1062.c`/LPUART1) -- all four shared the identical gap. Flagged as a plausible unexplained culprit for any past "display stopped responding" investigation, given the Genie link's continuous ~1250ms auto-ping traffic.

Verified `-fsyntax-only -Wall -Wextra` clean for all four files against the real SDK. Both repos synced. **Not yet hardware-tested -- expected to be the real fix.**

**Follow-up: overrun fix helped (no more permanent stalls) but brief self-resolving gaps remained -- plus a separate, real torn-frame-loss bug fixed.** User: "This code needs to be rock solid... write new code that can buffer any part message and tack this onto the next uart buffer read." Root cause: `process_uhf_reading()` read into a stack-local buffer each poll and discarded whatever `uhf_process_buffer()` didn't finish consuming (a frame declared longer than what was currently available) when that buffer went out of scope -- a real, independent gap from the overrun bug, causing transient loss any time a read's 3ms window happened to end mid-frame. Added a reassembly buffer: `uhf_carry_buf[512]`/`uhf_carry_len` in `app_context_t`, sized over the largest possible single frame (262 bytes). `process_uhf_reading()` now prepends any carry-over to each new read before parsing, and re-captures whatever's still unconsumed afterward. Confirmed the carry-over always starts exactly at a sync byte (never mid-frame), so prepending it is always safe. Also reset `uhf_carry_len` on a fresh `uhf_reader_open()` so a stale carry-over from a previous session can't misalign a new one. Verified `-fsyntax-only -Wall -Wextra` clean at both `APP_ENABLE_UHF` extremes. Both repos synced. **Not yet hardware-tested.**

Also, per explicit report ("we cannot get [temperature] after the
reader starts reading, only when idle... comment its operation out"):
`uhf_reader_stop()`'s trailing `uhf_reader_get_temperature()` call
(was the second command in `StopReaders()`) is now commented out --
wasn't reliable right after `stop_reading` and isn't currently used.
Function itself left intact. Updated the one host test this broke
(`test_uhf_reader.c`: `test_stop_sends_stop_then_temperature_query` ->
`test_stop_sends_stop_command`, now asserting 1 write instead of 2).

**Follow-up: beep sequencing changed (single beep on button press, double beep once reading actually confirmed started) and buzzer dropout root-caused a second time.** Per explicit instruction, `app_uhf_reader_control()`'s button-press beep changed from 2 pulses to 1; a new 2-pulse confirmation beep added inside the antennas-confirmed branch (where `fan_on()` already lives -- the only branch meaning the reader genuinely began inventory). Separately, `UHF_READ_BUZZER_TIMEOUT_MS` was briefly set to 200 (an unverified guess) then corrected to 300 once the user pasted the real source line (`BEEPDELAY`, confirmed 300 in `ACTIVERFID_V1.02_UHF.c`). Buzzer dropout still persisted at the correct value -- found a SECOND blocking call causing it: `genie_write_str()` (behind the `GENIE_TXPDR_STR` live-update) blocks up to 1250ms waiting for the display's ACK, firing on every tag-code change, more frequent during a real multi-tag stream than the nand_log flush already fixed once. Rather than add a third point-fix, refactored `process_uhf_reading()` to one general mechanism: a `had_bytes` flag (set when this call's transport read returned something) gates a single refresh of `uhf_last_read_ms` (fresh `systick_ms_now()`) at the very end of the function, after all of this call's own processing -- covering the display write, the flush, and any future blocking call added to the same function. Verified `-fsyntax-only -Wall -Wextra` clean at both `APP_ENABLE_UHF`/`APP_ENABLE_DISPLAY` extremes. Both repos synced. **Not yet hardware-tested.**

## Session update, 2026-07-17 continued: `APP_ENABLE_NRF_SPI` staged on

Per explicit instruction ("enable nrf"), flipped `APP_ENABLE_NRF_SPI`
0->1 in `bringup_config.h`. Unlike `APP_ENABLE_UHF` earlier this
session (which needed a real missing-`uhf_transport_rt1062_init()`-call
fix before it could be enabled), this stage was already fully wired
through the CURRENT `app_init()`/`app_loop()`/`app_pc_dispatch.c`/
`app_genie_dispatch.c` flow -- confirmed by grep across all four files:
transport init + the confirmed-required GPS bus-priming step in
`app_init.c`, `process_nrf_spi()` polling in `app_loop.c`, and reader-
power/channel/BT-advertising/playback/sleep-mode/DFU commands in both
the PC and Genie touchscreen dispatchers, all already correctly
`#if APP_ENABLE_NRF_SPI`-gated from earlier work. Just needed the flag
flip -- no code changes. Verified `-fsyntax-only -Wall -Wextra` clean
(exit 0) with the flag ON across all six touched files (`app_init.c`,
`app_loop.c`, `app_pc_dispatch.c`, `app_genie_dispatch.c`,
`nrf_spi_transport_rt1062.c`, `nrf_spi_protocol.c`) against the real
SDK tree; both repos already in sync, no drift to reconcile.

Current staged flags: Display+TCP+BMS+TIME_SYNC+GPS+STORAGE+BOARD_IO+
UHF+**NRF_SPI** all on; still off: REWIND, GPRS.

**CORRECTION: the "confirmed working" report above was a
misunderstanding -- user's actual report is "I do not see nrf fw".**
Real cause found immediately: `app_init.c`'s fw_version read had its
own `TODO: surface fw_version somewhere (debug UART print)` left
directly in the comment -- `nrf_spi_get_fw_version()` was called and
the result silently discarded, no PRINTF ever existed for it. Not a
hardware/link failure -- the console genuinely had nothing to show.
Fixed: added a PRINTF for both the fw_version byte AND the previously-
discarded `nrf_spi_status_t` return value (distinguishes a real
transfer failure from a successful read of the SPIS idle-default byte).
Verified `-fsyntax-only` clean, both repos synced. **NRF_SPI's actual
hardware status on this board is still unconfirmed** -- next reflash is
the real first test.

**LPSPI3 mode/baud: regression found, "fixed" via the Peripherals GUI,
then REVERTED after breaking GPS -- net result, back to square one.**
Real `board/peripherals.c`'s `LPSPI3_config` was found at the stock
Config-Tools default (500kHz, Mode 0) instead of the 1MHz/Mode-1 setting
project memory says was hardware-confirmed back on 2026-07-14 -- a real
regression (exact cause of the reversion not identified). Fixed via the
GUI (file locked for direct edits, same class of issue as `pin_mux.c`)
to `baudRate=1000000UL`, `cpha=kLPSPI_ClockPhaseSecondEdge` -- confirmed
landed correctly. Result: the nRF fw_version query still read
`status=0, byte=0x00`, **zero change** -- but GPS UBX messages broke
("ubx messages now stuffed"). Reverted back to 500kHz/Mode 0 via the
GUI. **Lesson**: the 2026-07-14 "Mode 1 safe for GPS" confirmation
PREDATES the GPS SPI pad drive-strength fix (max drive strength + fast
slew, confirmed 2026-07-16) -- that specific combination (faster clock +
stronger/faster pad edges) was never actually tested together before
this session, so the old confirmation didn't carry over. Since the mode
change helped the nRF query not at all, SPI mode/baud is likely not the
real blocker for that link -- current leading theory is the nRF52833
module may simply not be populated on this particular test board
(consistent with this board's established pattern of missing circuits).
**Do not re-attempt an LPSPI3 mode/baud change for the nRF's sake
without first confirming the module is physically present** -- there is
now direct evidence it doesn't help and only risks GPS.

**Revert CONFIRMED working** -- user: "ubx fixed", GPS back to normal
after reverting to 500kHz/Mode 0 via the GUI. Separately confirmed the
nRF52833 module IS populated/powered on this board. With mode/baud, CS
pin mux/pad, CS setup delay, and the ready-line-wait question all now
ruled out, software-side theories for the `status=0, byte=0x00` result
are exhausted -- the original 2026-07-14 investigation was ultimately
closed out with an oscilloscope directly confirming CS/SCK/MOSI activity,
and the same instrumentation (or at minimum an SPI wiring continuity
check, MISO especially) is the recommended next step before any further
code changes.

## Session update, 2026-07-17 continued: nRF SPI `fw_version` RESOLVED (`0x07` confirmed)

The user did the scope investigation after all, comparing the RT1062
directly against the real, working Rabbit reference hardware,
signal-for-signal. Real measurements taken (ground truth, not source
inference): SPI clock 320ns period = **3.125MHz** (not the ~1MHz
assumed since 2026-07-13 from an unvalidated datasheet-max guess);
CPOL=0/CPHA=1 = **Mode 1** (matches the nRF's own documented
`NRF_SPIS_MODE_1` requirement -- also resolved an apparent contradiction
with the real source's `SPImode(SPIMODE)` call, `#define SPIMODE 2`:
Rabbit's own internal "mode 2" numbering is a different convention than
NXP's textbook CPOL/CPHA-based "Mode 1" naming, not a developer error);
CS1 setup time **~200us** (vs. the 100us the code had, and vs. this
session's own ~30us busy-loop estimate); CS1 hold time **0us**
(matches the real `comms_NRF()` source exactly).

Fixes applied to match: `NRF_SPI_CS_SETUP_DELAY_US` 100u->200u
(`nrf_spi_transport.h`); `LPSPI3_config.baudRate` 500kHz->3125000UL via
the Peripherals GUI (Mode 1 was already correct from earlier
experiments). Hit one interim regression along the way: at 3.125MHz
with Slow slew rate (left over from earlier GPS experiments), MISO went
completely flat (`0x00`, no rise at all) -- diagnosed as the nRF's own
MISO driver unable to keep up with the much shorter clock period at
slow slew; fixed by switching `SPI_CLK`/`SPI_SDO`/`SPI_CS2`/nRF's CS pad
config back to Fast slew (`0x1079`, max drive).

**The actual key insight**: the nRF's own firmware only returns the
genuine `fw_version` on the very FIRST SPI transmission since its own
boot -- every earlier test that day (all the mode/baud/slew
experiments) had already consumed that window on the nRF's side, so no
amount of RT1062-side correctness could show `0x07` without a genuine
fresh nRF boot. Also, critically, **the successful test was run with
the debugger disconnected** -- this project has an already-confirmed
history of debug-probe/JTAG-pin interference causing real signal
corruption (see the ENET/PHY hang investigation, project memory
`project_enet_lwip_bringup`) -- likely explains some of the day's
confusing intermediate scope/breakpoint-based results, independent of
whatever mode/baud/timing changes were being tested at the time.

**Final confirmed-working configuration**: Mode 1
(`kLPSPI_ClockPolarityActiveHigh`/`kLPSPI_ClockPhaseSecondEdge`), baud
3125000UL, `NRF_SPI_CS_SETUP_DELAY_US`=200u, CS hold=0 (no delay), pad
config `0x1079U` (max drive, Fast slew) on `SPI_CLK`/`SPI_SDO`/
`SPI_CS2`/nRF's CS, debugger disconnected for real-world testing.

**GPS UBX re-tested in this exact config -- still failing, genuinely**
(confirmed with the debugger disconnected, ruling out the same
debug-probe-interference explanation that fixed the nRF). Checked
GPS's own CS2 timing directly against the real `NEOM8T.lib` source --
already matches exactly (1ms setup delay, 0 hold delay, same structure
our port already implements) -- so there's no further source-comparison
lead for GPS specifically. Next step: apply the same scope methodology
that resolved the nRF (breakpoint at `neo_m8t_transport_rt1062.c` line
203, observe MOSI/CS2/MISO directly during a real UBX command) to
distinguish a genuine electrical/signal-integrity problem from a
parsing bug in `neo_m8t_reader.c`'s own ACK-recognition logic
(`ubx_find_sync()`/`ubx_classify_ack()`) -- MISO showing real NMEA/UBX
chatter that our own code just doesn't recognize would point at the
latter, not more hardware tuning.

## Session update: GPS's speculative bus-priming step removed

During the scope investigation, traced the SPI transaction firing right
after `buzzer_off()` and confirmed it's `neo_m8t_configure_timepulse()`'s
own priming read (`neo_m8t_reader.c`) -- a SEPARATE, later priming step
from the nRF's own (which runs earlier, during the buzzer-ON window, in
`app_init.c`). Per explicit instruction ("the dynamic C code works -
it is the blueprint"): the real source only has ONE priming read total,
before `comms_NRF(0x0E)` specifically -- GPS itself is never primed in
the original, since it's always the first SPI user of the whole Rabbit
boot. GPS's own priming was a speculative, non-source-backed addition
from 2026-07-16, added to chase an intermittent reliability symptom
whose real causes (wrong SPI mode, wrong CS setup timing, wrong clock
speed, debug-probe interference) have now been found and fixed properly
via the scope work above -- making the workaround obsolete. Removed
entirely; GPS's first real transaction is now `ubx_pm2` directly,
matching the original exactly. Verified `-fsyntax-only` clean, both
repos synced. Not yet hardware-tested.

## RESOLVED: GPS UBX ACKs and nRF fw_version both confirmed working simultaneously

Long saga (spans 2026-07-13 through 2026-07-18) -- see project memory
for the full blow-by-blow. Final architecture:

- **`board/peripherals.c`'s `LPSPI3_config` is PERMANENTLY Mode 0**
  (CPOL=ActiveHigh/CPHA=FirstEdge), 3.125MHz baud -- GPS's own true
  native/default mode (confirmed via the u-blox datasheet AND
  empirically -- GPS only ever ACKed at Mode 0 all session). GPS's own
  code never touches SPI mode; `ubx_prt` is back in its original,
  fully source-faithful last-in-sequence position in
  `neo_m8t_configure_timepulse()`.
- **The nRF's Mode 1 requirement is fully self-contained inside its own
  transport layer** (`nrf_spi_transport_rt1062.c`'s `rt1062_transfer()`):
  switches into Mode 1 immediately before each of its own transfers,
  switches straight back to Mode 0 immediately after -- using the
  TCR-register-level mechanism scope-verified on 2026-07-14 (disable,
  rewrite only `LPSPI_TCR_CPOL_MASK`/`CPHA_MASK`, re-enable). GPS never
  sees anything but Mode 0, at any point, ever.

An earlier attempt (one-time bus-wide switch: send `ubx_prt` first at
Mode 0, then leave the WHOLE bus at Mode 1 for the rest of boot) got
the nRF working but GPS's remaining CFG messages still failed
completely -- proving GPS genuinely cannot tolerate Mode 1 on this
hardware, unlike the real Rabbit reference (which runs both devices at
a single Mode 1 successfully). That approach was fully reverted in
favor of the per-transfer architecture above, which needed none of the
bus-wide-switch machinery (`neo_m8t_send_cfg_prt_at_boot()`,
`app_init.c`'s bootstrap block -- both removed).

**CONFIRMED on real hardware, clean power cycle, debugger
disconnected**: both nRF `fw_version` AND GPS's `UBX cfg
pm2/navx5/nav5/tp5/gnss/prt` all ACK successfully. Verified
`-fsyntax-only -Wall -Wextra` clean at both `APP_ENABLE_GPS`/
`APP_ENABLE_NRF_SPI` flag extremes, both repos synced.

**Lessons worth remembering for similar shared-bus problems**:
1. A single shared SPI mode/speed working for two different-native-
   requirement devices on ONE reference implementation (the real
   Rabbit) doesn't guarantee the same tolerance holds on a different
   physical implementation (this RT1062 port).
2. Per-transfer mode switching localized to the pickier device's own
   transport layer is a clean, low-risk architecture for a shared bus
   with genuinely conflicting requirements -- better than hunting for
   one shared setting or a fragile one-time mid-boot switch.
3. Debug-probe/JTAG interference during live scope+breakpoint sessions
   produced misleading "fail"/"regression" results TWICE in this exact
   investigation, indistinguishable from real bugs without a genuine
   power cycle with the debugger fully disconnected.
4. When verifying datasheet facts (bit positions, endianness), extract
   the actual tables directly rather than relying on general
   recollection -- caught real errors this session (wrong spiMode bit
   position, wrong endianness assumption) that would have propagated
   into wrong conclusions otherwise.

## Session end, 2026-07-17

Board left at: `LPSPI3_config` Mode 1 (CPOL=ActiveHigh/CPHA=SecondEdge),
baud 3125000UL, pad config `0x1079U` (max drive, Fast slew) on
`SPI_CLK`/`SPI_SDO`/`SPI_CS2`/nRF's CS, `NRF_SPI_CS_SETUP_DELAY_US`=200u,
CS hold=0 for both devices. **nRF SPI CONFIRMED WORKING**
(`fw_version=0x07`) in this exact config, debugger disconnected. **GPS
UBX still not getting ACKs** in this same config -- see above for the
recommended next step (scope GPS's own transaction the same way, to
tell apart an electrical issue from an ACK-parsing bug in our own code).

## Directory layout

- `firmware_source/` -- the port itself (~90 files)
- `real_project_config/` -- **mirrored, NOT live-synced** copies of the
  four real-project-only config files that most frequently hold the
  actual bug (`.cproject`, `lwipopts.h`, `board/pin_mux.c`,
  `board/peripherals.c`) -- added 2026-07-21 specifically because these
  files were previously untracked ANYWHERE, meaning there was no "before"
  state to diff against when something broke. Same manual-copy workflow
  as `firmware_source/`'s own real-project sync: after editing any of
  these four in the real MCUXpresso project
  (`C:\Users\rfidt\Documents\MCUXpressoIDE_11.9.1_2170\workspace\eaimxrt1062_hello_world\`),
  copy the changed file into this directory and commit -- git will never
  pick up the real project's copy on its own, it lives outside this repo
  entirely.
- `host_tests/` -- pure-logic unit tests, runs on any machine with gcc
- `reference_alternatives/` -- superseded code paths kept for reference
- `reference_dynamic_c/` -- CANONICAL original Dynamic C source, copied
  2026-07-15 from the live IDE install (`C:\DCRABBIT_10.72E`, NOT the
  various dated "Backups" folders under Dropbox/RFID RTS Files, which
  can be stale -- confirmed the hard way when an old NEOM8T.lib backup
  was missing all its UBX protocol code). `ACTIVERFID_V1.02_UHF.c` is
  THE authoritative main program for this port -- confirmed by explicit
  instruction ("We are not using ActiveRFIDV1.01"); no other
  ACTIVERFID version belongs in this tree. Plus `UHF_READER.LIB`,
  `NEOM8T.lib`, `Genie2.lib`, `FinishLynx.lib` -- the actual libraries
  in use, not older exports. Re-copy from `C:\DCRABBIT_10.72E` if these
  ever need refreshing (the user actively edits that live tree).
- `INTEGRATION.md`, `OTA_MCUBOOT_INTEGRATION.md` -- deeper subsystem notes
