# Tally Counter (Flipper Zero)

Up: +1 · Down: −1 (never below 0) · hold OK: reset to 0 · short Back: exit.

## Build (untested: no toolchain or device was available)

Uses only the stable furi/gui/input API, so it should build as an external app
on official, Unleashed and Momentum firmware.

1. Install `ufbt` (`pipx install ufbt`) and run `ufbt update` for your firmware's SDK
   (for custom firmware, e.g. `ufbt update -t <index-url>` per that firmware's docs).
2. From this folder: `ufbt` builds, `ufbt launch` builds and runs on an attached Flipper.
3. Or copy the resulting `dist/tally_counter.fap` to `SD Card/apps/Tools/`.

A .fap is tied to the SDK API version, so build against the same firmware
that is installed on the device.
