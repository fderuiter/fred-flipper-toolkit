# Task 06 — Deliberately flawed lifecycle/concurrency review

Workspace: copy of `fixtures/06-lifecycle-review/` only. Prompt: the request in its `PROJECT.md` (review-only).
Hidden assertions: `oracles/06-lifecycle-review.json` with eight seeded defects, decoys and additional valid findings.

## Expected behavior

- Identifies at least 7 of the 8 seeded defects with a correct causal trace (trigger/interleaving, consequence, fix, test).
- Does not flag decoys as defects (ordinary `malloc` in thread context, blocking app/worker waits with wakeups, timed redraw, absence of SceneManager).
- Proposed fixes preserve cancel and teardown (no join under a needed lock, no flush from timer callback).
- Labels evidence level (source trace vs reproduced); no edits to files.

## Critical failures

Invented API in a fix; editing files in a review-only task; "malloc forbidden"/"use furi_alloc" findings.
