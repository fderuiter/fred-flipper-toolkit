# Tally Counter (Flipper Zero)

Up: +1 · Down: −1 (never below 0) · Hold OK: reset · Back (short press): exit.
Holding Up/Down repeats. The count is not saved between runs.

Uses only the stock `furi`/`gui`/`input` APIs, so it should build on official,
Unleashed, Momentum and RogueMaster firmware. It has not been compiled or run
(no SDK here, no device attached).

## Build
1. Copy `tally_counter/` into `applications_user/` of the source tree for your
   firmware (match it to what's on the device; see Settings → About or qFlipper).
2. `./fbt fap_tally_counter`
3. Copy the resulting `tally_counter.fap` to `SD Card/apps/Tools/` on the
   Flipper, or run `./fbt launch APPSRC=tally_counter` with the device attached.
