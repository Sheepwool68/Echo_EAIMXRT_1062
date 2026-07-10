# ActiveRFID -> i.MX RT1062 Port: Integration Guide

## What this is, and isn't

This is **not** a ready-to-import `.cproject`/`.project` pair. Building one of
those correctly requires the actual NXP MCUXpresso SDK for your exact board
revision -- CMSIS device headers, startup assembly, the linker script (with
your board's real flash/RAM memory map), and the lwIP/littlefs library source
itself. I don't have access to that SDK, and hand-writing a linker script or
startup file from memory is exactly the kind of thing that can look plausible
while being subtly wrong (wrong memory region, wrong vector table) in ways
that waste far more of your time debugging than being upfront now.

What you get instead:

- `firmware_source/` -- every `.c`/`.h` file from this port, ready to drop
  into a real MCUXpresso project's source folder. No test files, no stub
  SDK headers (those were only ever for me to syntax-check code in this
  conversation) -- just the actual firmware source.
- `host_tests/` -- the ~130 unit tests built up over this whole port, plus a
  `Makefile` that builds and runs all of them with your PC's `gcc`. These
  test the *pure logic* modules (protocol parsing, CRC, calendar math,
  record builders) and need nothing target-specific. Run `make` in this
  folder any time you change something in `firmware_source/` to catch
  regressions before you touch hardware.
- This file, with the steps to get a real, correctly-configured
  MCUXpresso project and drop the firmware source into it.
- `OTA_MCUBOOT_INTEGRATION.md` -- a separate, focused guide for the
  over-the-air firmware update path specifically (HTTP download,
  CRC verification, writing into MCUboot's secondary slot). Read this
  one only once the base firmware from this file's steps is already
  building and running -- OTA is a later-stage concern, not day one.

## Step 1: Get Embedded Artists' own patched SDK (not the generic NXP one)

**Correction from earlier in this conversation**: I'd originally pointed you
at NXP's generic SDK Builder. Embedded Artists publishes their own
pre-patched SDK specifically for this board, and it's the right one to use
-- confirmed via their own migration documentation:

- Your board (rev C1, product EAC00308, 16MB QSPI flash -- matching the
  IS25WP128 already confirmed earlier in this conversation) needs
  **`eaimxrt1062_16mb_sdk_2_12_1_<date>.zip`**, downloaded from
  **sw.embeddedartists.com** (search "imxrt1062" there, or use the direct
  link on their [migration guide page](https://developer.embeddedartists.com/docs-mcu/imxrt-dev-guide/proj-migration/)).
  Do **not** use the 4MB variant (`eaimxrt1062_4mb_sdk_...`) -- that's for
  rev ≤ B2 boards, not yours.
- This SDK already has the correct flash driver/size baked into its
  examples -- confirmed from EA's own docs: `BOARD_FLASH` size `0x1000000`
  (16MB) and driver `MIMXRT1060_SFDP_QSPI.cfx` (not the `EcoXiP_ATXP032.cfx`
  driver older/smaller-flash boards use). If you ever import an *old*
  project or an example from the wrong SDK version, EA's migration guide
  above walks through fixing exactly these settings -- but starting fresh
  from the correct 16MB SDK, you shouldn't need to touch this at all.
- Confirm you're also selecting/have available: **lwIP** and **littlefs**
  (both are standard MCUXpresso SDK middleware, included in EA's SDK the
  same way -- EA patches the flash/board config, not the middleware set).

## Step 2: Import a bare-metal lwIP example as your starting project

In MCUXpresso IDE:

1. Drag the EA SDK zip onto the **Installed SDKs** panel to install it.
2. Click **Import SDK example(s)...** in the Quickstart panel. Note EA's
   examples are based on NXP's **`evkbmimxrt1060`** board name even though
   your board is the RT1062 OEM -- that's expected (the RT1060/RT1062 are
   close enough in silicon that NXP's example tree uses one board name),
   not a sign you picked the wrong thing.
3. Look for an lwIP example with **"bm"** (bare-metal) in its name --
   *not* one with "freertos" in the name. NXP's SDK ships both variants;
   this port needs the bare-metal one, matching the raw/callback API
   `tcp_transport_lwip.c` was written against (confirmed separately: NXP's
   own docs describe this as "mainloop mode (bare metal mode) with raw
   API").
4. Finish the import. This gives you a project with a **correct, working**
   CMSIS/startup/linker/driver/flash setup for your exact board -- the part
   I couldn't safely fabricate myself.

## Step 3: Drop in this port's firmware source

1. Delete (or rename out of the build) the example's own `main.c` -- the one
   from this port (`firmware_source/main.c`) replaces it.
2. Copy everything from `firmware_source/` into the project's `source/`
   folder (or add it as a new source folder via project properties -- either
   works, MCUXpresso just needs the files somewhere on its build path).
3. Add `firmware_source/` (or wherever you placed it) to the project's
   include paths, alongside whatever the SDK example already added for
   itself.

## Step 4: Configure `lwipopts.h`

The SDK example already provides an `lwipopts.h` -- you'll need to adjust it
for this port's needs:

- `NO_SYS` must be `1` (bare-metal, no RTOS -- this is the whole point of
  starting from the "bm" example rather than the FreeRTOS one).
- `LWIP_TCP` and `LWIP_UDP` must be `1`.
- `MEMP_NUM_TCP_PCB` needs to cover at least 4 simultaneous TCP PCBs (3 data
  client slots + 1 reset socket) -- check the example's default isn't lower.
- `TCP_MSS` should match your network's real MTU (1460 for standard Ethernet
  is the usual default; not something I can verify for your network).

## Step 5: Staged bring-up

Start with `firmware_source/bringup_config.h` exactly as shipped (only
`APP_ENABLE_TCP` on). Build, flash, connect from a PC with telnet/netcat to
port 23, confirm you get the `Connected,...,U\n` greeting. Then work through
the flags one at a time in the order documented in that file's comments --
each one lists its dependencies and how to verify it before moving on.

## Known gaps, unchanged from before

- **Not yet verified**: whether EA's patched SDK zip bundles littlefs the
  same way stock NXP SDKs do, since EA's patching focuses on the flash/board
  config rather than the middleware set. If it's missing from the component
  list when you import, it's still available as a standard MCUXpresso SDK
  middleware -- add it via the SDK's component manager, or pull it from the
  official `mcuxsdk-middleware-littlefs` repo directly.
- `GENIE2.LIB` (display) and `NEOM8T.lib` (GPS) aren't ported -- clean stub
  interfaces (`display_stub.h`, `gps_stub.h`) are waiting for the real thing.
- `ConnectToSocketServer()` (outbound LAN/modem socket connect) was in the
  original `ActiveRFID.C` itself, not any library pasted so far --
  `outbound_connect_stub.h` flags this gap.
- Settings persistence (no userblock/flash-settings library was ever
  provided), the MP2731 battery charger, and Finish Lynx socket integration
  are all `TODO`-marked in the code, not silently assumed.
- The IPv4 address accessor macros in `tcp_transport_lwip.c`
  (`ip4_addr_get_u32` etc) are flagged as version-dependent -- verify against
  your actual lwIP version's headers.
