# OTA Firmware Update via MCUboot: Integration Guide

## What's built and tested (host-side, no hardware needed)

- `fw_version_check.h/.c` -- parses a version-check response, decides if
  an update's available.
- `crc32.h/.c` -- standard CRC-32, validated against the well-known
  reference vector.
- `http_response_parser.h/.c` -- incremental HTTP/1.1 response parser
  (status line, headers, Content-Length, streaming body).
- `fw_downloader.h/.c` -- orchestrates the GET, streams the body through
  a sink while computing a running CRC32.
- `qspi_flash_raw.h/.c` -- shared low-level flash primitives (program,
  erase, read), extracted from the littlefs backend so this and the
  MCUboot slot writer use the exact same tested code path.
- `fw_install_mcuboot.h/.c` -- writes the downloaded bytes into MCUboot's
  secondary slot, erasing sectors just-in-time, failing closed if the
  image would overflow the slot.

All of the above have host-side unit tests and pass under
`-Wall -Wextra -Wpedantic -Wconversion`.

## What you asked for, confirmed via NXP's own docs (not guessed)

- `examples/ota_examples/mcuboot_opensource` in the MCUXpresso SDK is the
  bootloader itself -- confirmed working directly on an RT1060 (a real
  user's NXP community post shows them flashing it via MCUXpresso IDE and
  getting it running).
- `examples/ota_examples/ota_mcuboot_server_enet` is NXP's own
  Ethernet-based OTA demo built on top of it -- the direct sibling to
  what you're building here.
- RT1060's FlexSPI supports MCUBoot's DIRECT-XIP flash-remapping mode:
  zero-copy swap, fastest and least flash wear, but no automatic
  rollback (vs the default SWAP algorithm, which is slower/more wear but
  can revert a bad update on its own). Worth deciding which you want once
  you're at that step -- not something this port picks for you.
- Images are signed at BUILD time via `imgtool` (ships in
  `middleware/mcuboot_opensource/scripts`, or `pip install imgtool`) --
  nothing about signing happens on the device.

## Building and flashing the bootloader in MCUXpresso IDE

**Before starting**: EA's patched SDK zip may or may not bundle
`examples/ota_examples/mcuboot_opensource` depending on which SDK
baseline it was built against. If MCUXpresso's "Import SDK example(s)"
picker doesn't show `mcuboot_opensource` for your board after installing
EA's SDK, you may need to pull that specific example folder from a
vanilla NXP SDK Builder build for the same board/SDK version and drop it
alongside EA's installed SDK -- verify this by actually opening the IDE
and checking; it's not something confirmable remotely.

This is two SEPARATE MCUXpresso projects, built and flashed in order --
not one project with two build targets.

### Part 1 -- get the bootloader into flash

1. **Import SDK example(s)...** in the Quickstart panel, select your
   board, find `mcuboot_opensource` under the OTA examples category.
2. Build it normally.
3. **Launch a debug session** on this project -- this both programs the
   flash and starts running it. You should see a version banner over
   your debug UART ("Bootloader Version x.x.x... Unable to find bootable
   image" is *expected* here -- there's no app yet).
4. **Stop the debugger.** The bootloader is now permanently resident in
   flash; you don't need to keep debugging it. Note this project's own
   `flash_partitioning.h` -- `BOOT_FLASH_ACT_APP` / `BOOT_FLASH_CAND_APP`
   addresses and slot sizes from *this specific build*. Every signing
   command below must use these exact numbers, not generic example
   values -- they're specific to how this project was configured for
   your board.

### Part 2 -- get a signed app into the primary slot

1. Import and build a *second* project -- for the first test, use NXP's
   own `hello_world` example, not your actual app yet.
2. MCUXpresso often only produces a `.axf` (ELF), not a flat `.bin`.
   Right-click the built `.axf` under the project's **Binaries** folder
   -> **Binary Utilities** -> generate the raw binary.
3. Sign it with `imgtool` (in `mcuboot_opensource`'s `scripts` folder, or
   `pip install imgtool`), using the exact slot size/header size from
   Part 1 step 4:
   ```
   imgtool sign --key sign-rsa2048-priv.pem --align 4 --header-size 0x400 \
     --pad-header --slot-size <YOUR_SLOT_SIZE> --max-sectors 800 \
     --version "1.0" --pad --confirm hello_world.bin hello_world_SIGNED.bin
   ```
   The private key ships somewhere under the bootloader project's own
   tree (look for a `keys` folder under `mcuboot_opensource` -- the exact
   path has moved between SDK versions, so search rather than trust one
   hardcoded location).

   **Three flags that are easy to conflate, and shouldn't be:**
   - `--pad-header` reserves space for the image header -- use on every
     signed image.
   - `--pad` pads the image out to the full slot size and writes trailer
     info.
   - `--confirm` marks the image as pre-confirmed.

   **`--pad --confirm` are only needed for THIS first manually-flashed
   image.** Later OTA-delivered images signed for the secondary slot
   skip them -- per NXP's own docs: *"Signed images used in OTA process
   do not require '-pad' parameter."* Getting this backwards (using
   `--pad --confirm` on OTA updates, or omitting them on the first
   manual flash) is the documented cause of the "Unable to find bootable
   image" error even when an image is physically present.
4. Flash the signed binary to the **primary slot address** (from Part 1
   step 4) using your debugger/programmer.
5. Launch the debug session on this app project. **Real, documented
   gotcha**: MCUXpresso's debugger typically halts at the app's own
   reset vector, but execution actually needs to pass through the
   bootloader first -- you'll likely see it **stall in an endless loop
   inside the bootloader**. Pause debugging and type `jump ResetISR` in
   the debugger console to force it into the app's actual entry point.
6. If that works, you should see `hello_world`'s normal output -- now
   running *through* the bootloader, not standalone.

Only after that succeeds should you swap `hello_world` for this port's
`main.c` in that second project, and only then move to the OTA modules
below (`fw_downloader.c` -> `fw_install_mcuboot.c`).

## Testing an actual OTA cycle, once the above boots

Test a real OTA cycle: `fw_download_firmware()` into
`fw_mcuboot_install_sink`, confirm the CRC matches what you expected,
call `fw_mcuboot_install_finalize()`, reset, confirm MCUboot picks up
the new image.

## The one thing that still needs your verification

`fw_install_mcuboot.c` calls `boot_set_pending_multi(0, 0)` to tell
MCUboot "try this image next boot" -- declared as an `extern` in that
file rather than assumed correct, because the exact function name has
changed across MCUboot versions (older releases used `boot_set_pending()`
without the `_multi` suffix). Once you have the real
`mcuboot_opensource/boot/bootutil/include/bootutil/bootutil_public.h`,
grep it for `boot_set_pending` and confirm the signature matches, or
replace the `extern` declaration with `#include`-ing that header
directly. This fails safe either way -- a wrong function name is a
build-time link error you'll see immediately, not a runtime surprise
after a botched field update.

Also: don't skip wiring up MCUboot's **image confirm** step in your
app once it successfully boots a new image. Without it, MCUboot's
swap-mode revert logic will roll the device back to the previous
firmware on the *next* boot, silently undoing every update after one
power cycle -- which looks like "OTA doesn't work" but is actually
MCUboot's rollback safety net doing exactly what it's designed to do.
