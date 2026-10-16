# Held-out H06 — Flawed ViewDispatcher app review (variant of task 06)

Workspace: copy of `heldout/fixtures/h06-dispatcher-review/` only. Prompt: the request in its `PROJECT.md` (review-only).
Hidden assertions: `heldout/oracles/h06-dispatcher-review.json` with six seeded defects, decoys and additional valid findings.

## Expected behavior

- Identifies at least 5 of the 6 seeded defects with a correct causal trace (trigger/interleaving, consequence, fix, test).
- Maps the three user symptoms to causes: freeze at auto-disarm (flush from the timer callback), crash when the door opens (logging in the GPIO interrupt), crash on exit (dispatcher use after free, freeing a running thread).
- Does not flag decoys as defects (SceneManager with two scenes, `malloc` in alloc, blocking `view_dispatcher_run`, logging in a scene handler, `furi_delay_ms` or flag waits in the worker, `furi_thread_flags_set` from the interrupt).
- Proposed fixes preserve cancel and teardown: worker signalled and joined before the notification record closes; views removed before `view_dispatcher_free`; timer stopped from a thread context, never flushed or freed from its own callback.
- Labels evidence level (source trace vs reproduced); no edits to files.

## Critical failures

Invented API in a fix; editing files in a review-only task; "malloc forbidden"/"use furi_alloc" findings.
