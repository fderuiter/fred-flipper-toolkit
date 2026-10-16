# Device qualification protocol (issue #9)

**Status: not run.** No Flipper Zero was available to the implementation sessions. This protocol makes the run repeatable for an operator with hardware; it does not authorize anything by itself. Record every observation in `results.md` (copy the table at the end) and keep failures.

## Scope and authority (fill before starting)

| Item | Value |
|---|---|
| Operator | |
| Device | Flipper Zero, hardware revision from *Settings → About* |
| Firmware | official **1.5.1** (`9e810dc`), confirmed on the device's About screen; do not flash as part of this protocol unless separately authorized |
| Authorized actions | copy `.fap` files and test data to the SD card; launch the three apps; connect a **3.3 V** USB-UART adapter (adapter TX → header pin 14 RX, adapter GND → pin 8/11/18); send synthetic captures from the host. No other GPIO wiring, no 5 V on any signal pin, no radio transmission, no flashing. |
| Stop procedure | Back/exit the app; if the device hangs, hold Left+Back to reboot; disconnect the adapter |
| Data | synthetic fixtures only; no real captures or identifiers in results |

## Artifacts

Build with `evaluations/harness/build_fixtures.sh` (uFBT 0.2.6, verified 1.5.1 SDK, toolchain 39) and record the SHA-256 of each `.fap` you deploy. Expected hashes for single-source apps from the 2026-10-16 qualification: `fred_tally` `2d2c389c55db…`. Multi-source apps may differ by object order (see the [qualification record](../results/2026-10-16-sdk-qualification/README.md)); record what you deploy.

Copy to `/ext/apps/Tools/` (or any apps folder) with qFlipper or the SD card. App data folders are `/ext/apps_data/<appid>/`.

## Cases

### A. Navigation, exit and memory (fred_tally, fred_interval_timer)

1. Note free heap from *Settings → System* or the CLI `free` command (if CLI use is authorized).
2. `fred_tally`: Up ×5, Down ×7 (count stays ≥ 0), long OK (resets to 0), hold Up (repeat increments), short Back exits.
3. `fred_interval_timer`: Menu → Settings, change each item, Back → Menu → About → Back → Start; let one work/rest transition happen (vibration if enabled); Back during the countdown returns to the menu; Back from menu exits.
4. Repeat open/work/exit 20 times for each app; record free heap after runs 1, 10 and 20 (must stabilize, not decrease steadily).

### B. Settings recovery (fred_interval_timer)

For each case, place the file at `/ext/apps_data/fred_interval_timer/settings.txt`, launch, open Settings, then exit:

| File | Expected |
|---|---|
| none | defaults (Work 30 s, Rest 10 s, Rounds 8, Vibrate On); no file is created until a value is edited, then it is written when a menu item is selected or on exit |
| `samples/settings_v1.txt` | Work 45, Rest 15, Rounds 8, Vibrate On; file rewritten as Version 2 on exit (migration), old file kept as `settings.bak` |
| `samples/settings_v2_corrupt.txt` | defaults; file unchanged until a value is edited; after an edit and save the corrupt file becomes `settings.bak` |
| `samples/settings_v7_future.txt` | defaults in memory; About explains; **file unchanged** after exit (compare SHA-256) |
| SD card removed, a value edited, Back, then *Start* selected | no crash; save fails and is logged; countdown still runs; app exits normally |
| stale `settings.tmp` beside a valid `settings.txt` | valid settings load; tmp removed |

Interrupted-write recovery on hardware (power removal mid-save) is **optional and destructive to the test file only**; do it only with explicit authorization and a backed-up SD card.

### C. UART bursts, errors and cancel (fred_soil_uart)

Wiring: 3.3 V USB-UART adapter TX → pin 14, GND → pin 18. Do not connect adapter VCC.

1. Busy handle: if possible, enable the USART log/CLI on the header first; launch → expect "USART busy", Back exits.
2. `send_capture.py --port <adapter> burst_hex.txt` → Moisture 303, Temp 22.8 C, errors 0.
3. `send_capture.py --port <adapter> noise_hex.txt` → one reading (Moisture 300, Temp -1.5 C), errors ≥ 2.
4. Stress: `send_capture.py --repeat 2000 --gap 0 burst_hex.txt` → UI stays responsive; overflow counter may increase; no crash.
5. Cancel with active producer: start step 4 and press Back mid-stream → app exits within 1 s, no crash; relaunch works (no stale handle).
6. Repeat 5 ten times.

## Results table

| Case | Steps run | Observed | Repetitions | Result (pass/fail/not-run) | Notes |
|---|---|---|---|---|---|
| A2 | | | | | |
| A3 | | | | | |
| A4 heap (1/10/20) | | | | | |
| B (each row) | | | | | |
| C1–C6 | | | | | |

A stable native-support claim requires A, B (except the optional power-loss case) and C to pass on the recorded device and firmware. Radio, appliance and fork claims need their own tests.
