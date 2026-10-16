# Interval Timer FAP: design and evidence

Target: official firmware 1.5.1 (commit 9e810dc), f7, API 88.2.

## Architecture
ViewDispatcher with Submenu (main), custom View (settings), custom View (running), DialogEx (About / notices).
No SceneManager. Pure logic is split from Furi: `it_settings.c` (codec), `it_store.c` (load/save policy over an
abstract `ItFsOps`), `interval_timer.c` (UI + Furi storage binding).

## Settings policy
- Files in `APP_DATA_PATH`: `settings.fff`, `settings.tmp`, `settings.bak`.
- v1 (Work/Rest) loads with Rounds=8, Vibrate=true defaults; the user's Work/Rest are kept exactly (the settings
  screen steps by 1, not by a coarse grid, so odd values like 37 survive). The file is rewritten as v2 only when the
  user changes and leaves Settings; the v1 file is then kept as `.bak`.
- Invalid file (out of range, duplicate/missing key, truncated, no trailing newline, >512 B): whole-file reject.
- Future version (>2), corrupt, or unreadable SD: defaults in memory, a notice is shown, **saving is blocked** so the
  existing file is never rotated or overwritten.
- No file: defaults, saving allowed. `cur` missing but `.bak` present (interrupted save): `.bak` is used.
- Save: write tmp (check length, sync, close) -> remove bak -> cur->bak -> tmp->cur; on the last step failure it
  tries to restore bak->cur. Save failure shows a notice; the change stays dirty and is retried at exit.
  Not claimed power-loss atomic [C-STO-002].
- Storage runs only in the dispatcher thread (custom event / after Back), never in draw or input callbacks.

## Ownership table
| Resource | Owner | Borrowers | Acquire failure | Context | Synchronization | Stop / release |
|---|---|---|---|---|---|---|
| Storage, Notification, GUI records | app | all | open blocks until service exists | app thread | none | closed last |
| Dispatcher + views | app | GUI thread draw/input, dispatcher callbacks | OOM crashes (no NULL path) | GUI / dispatcher | locking view models | views removed, then freed, then dispatcher freed |
| Settings/run state (`App`) | app | dispatcher-thread callbacks only | n/a | dispatcher | single thread (settings model copied via `with_view_model`) | freed after dispatcher freed |
| Backlight enforce | app | run start | n/a | dispatcher | `backlight_forced` | `run_stop` on Back, Done, and exit |
| `File*` handles | each fs op | none | open false -> ItFsError | app thread | none | closed and freed in the same function |

No timers, threads or queues are owned by the app (the dispatcher tick drives the countdown), so there is no
callback-quiescence problem. Back/cancel is handled in the view previous callback and is never queued or dropped.

Teardown: `run_stop` (releases backlight) -> final save retry -> remove views -> free views -> free dispatcher ->
close records -> free app.

## Running screen
Deadline-based (`furi_get_tick`, wrap-safe), 250 ms tick, no drift accumulation. Rest=0 skips the rest phase; no rest
after the final round. Vibration at phase changes (double at the end) if enabled.

## Evidence (per evidence-policy fields)
| Field | Status | Notes |
|---|---|---|
| Documentation reviewed | not-run | no network/doc review in this session |
| Source inspected | blocked | no firmware tree or `api_symbols.csv` in the workspace |
| Static checks | not-run | Bash was denied in this session |
| Compiled | not-run | no SDK, no shell |
| Host tests | not-run | written (`tests/test_settings.c`), never executed |
| Device behavior | not-run | no device |

Host-test command (from `interval_timer/tests`):
`cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I.. test_settings.c ../it_settings.c ../it_store.c -o /tmp/it_test && /tmp/it_test ../../samples`

Build: pin an SDK tag matching 1.5.1 with an isolated `UFBT_HOME`, then `ufbt` in `interval_timer/`.

## Unresolved API assumptions (must be checked against API 88.2 `api_symbols.csv` before building)
`storage_simply_mkdir`, `storage_common_stat/remove/rename`, `storage_file_sync`, `sequence_single_vibro`,
`sequence_double_vibro`, `sequence_display_backlight_enforce_on/auto`, `view_dispatcher_set_tick_event_callback`
(tick period units assumed ~ms), `dialog_ex_*` signatures, `APP_DATA_PATH("")` with an empty suffix.

## Pending device cases
SD removed before launch / during save, v1 upgrade on a real card, future-version file left untouched, Back during
run, backlight and vibration restored after cancel, 99 rounds x 600 s endurance.
