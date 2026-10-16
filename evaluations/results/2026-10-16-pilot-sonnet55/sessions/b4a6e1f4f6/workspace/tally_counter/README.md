# Tally Counter (Flipper Zero)

Up +1, Down −1 (never below 0), long-press OK resets to 0, short Back exits. Holding Up/Down repeats.

Not compiled or tested: no toolchain or device was available.

Build:
1. Copy this folder into `applications_user/` of a firmware checkout (official, Unleashed, Momentum and RogueMaster share this layout), or use `ufbt` from this folder.
2. `./fbt fap_tally_counter` (or `ufbt`).
3. Copy the resulting `tally_counter.fap` to `/ext/apps/Tools/` on the SD card.

It uses only core `furi`, `gui` and `input` APIs, so it should build on any recent firmware.
