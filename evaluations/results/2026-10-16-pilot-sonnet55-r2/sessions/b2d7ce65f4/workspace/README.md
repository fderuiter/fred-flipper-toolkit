# Tally Counter (Flipper Zero)

Up = +1, Down = -1 (floors at 0), long-press OK = reset, short Back = exit.
Count is drawn in the big-number font in the centre.

## Build (not compiled or tested on a device)
Uses only the standard FAP API, so it should build on official, Unleashed,
Momentum and RogueMaster firmware. Use the SDK matching your firmware
(check Settings > System > About on the device if unsure).

1. `pip install ufbt`
2. For non-official firmware, point ufbt at that firmware's SDK index
   (`ufbt update --index-url=<index>`); official: `ufbt update --channel=release`.
3. In this directory run `ufbt` to build, or `ufbt launch` to build, install and run over USB.
4. Or copy `dist/tally_counter.fap` to `SD Card/apps/Tools/`.
