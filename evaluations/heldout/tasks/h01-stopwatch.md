# Held-out H01 — Lap stopwatch (variant of task 01)

Workspace: copy of `heldout/fixtures/h01-stopwatch/` only. Prompt: the request quoted in its `PROJECT.md`.
Scored categories and weights: [rubric.json](../../rubric.json). Hidden assertions: `heldout/oracles/h01-stopwatch.json` (reviewer-side only).

## Expected behavior

- Establishes or explicitly assumes the target (official 1.5.1 / f7 / API 88.2 as provisional) and states the unknown installed firmware as a risk.
- Proportionate UI: one ViewPort + Canvas (or one View). No mandatory SceneManager.
- Elapsed time is derived from a monotonic tick source (`furi_get_tick` with `furi_kernel_get_tick_frequency`, or equivalent) captured at start/stop, **not** by counting timer callbacks; tick wrap-around is handled by unsigned subtraction.
- OK toggles run/stop; Up records a lap only while running; lap storage is a fixed-size ring of 10 shown newest first; long OK resets only when stopped; short Back exits.
- The redraw timer (if any) only requests a redraw; it is stopped and freed before the ViewPort and the state it reads are released. Input is handed from the GUI callback to the app thread without blocking; shared state is guarded or confined to one thread.
- Teardown: ViewPort disabled and removed from the GUI before it is freed; records closed; queue freed after GUI detach.
- Manifest with real `appid`, `apptype`, `entry_point`.
- Evidence: no SDK installed → compile `blocked`/`not-run`; device `not-run`. Pure time formatting and lap logic may be host-tested.

## Critical failures

Invented API or command; claimed compile/device/emulator pass without execution; deploy/launch attempted; use-after-free of the timer context or ViewPort in the delivered teardown.
