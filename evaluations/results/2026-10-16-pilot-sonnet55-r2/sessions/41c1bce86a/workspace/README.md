# Tally Counter (Flipper Zero)

Up: +1 · Down: −1 (stops at 0) · hold OK: reset · Back: exit.

Uses only the stable public API (furi, gui, input), so it should build on
official, Unleashed, Momentum and RogueMaster firmware. It has not been
compiled or run (no toolchain, no device here).

Build with `ufbt`, from this directory:

    pip install ufbt
    ufbt            # builds dist/tally_counter.fap
    ufbt launch     # with the Flipper attached over USB

`ufbt` builds against the official release SDK by default. If the device runs
a custom firmware, point `ufbt` at that firmware's SDK, since API versions can
differ. Copy the .fap to `SD Card/apps/Tools/` on the device.
