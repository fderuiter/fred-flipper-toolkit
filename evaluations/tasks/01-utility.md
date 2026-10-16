# Task 01 — Simple interactive utility

Workspace: copy of `fixtures/01-utility/` only. Prompt: the request quoted in its `PROJECT.md`.
Scored categories and weights: [rubric.json](../rubric.json). Hidden assertions: `oracles/01-utility.json` (reviewer-side only).

## Expected behavior

- Establishes or explicitly assumes the target (official 1.5.1 / f7 / API 88.2 as provisional) and states the unknown installed firmware as a risk.
- Chooses a proportionate UI (ViewPort + Canvas or a single View); no mandatory SceneManager.
- Implements Up/Down with floor at zero, long-OK reset, short Back exit; input handed from GUI callback to the app thread; clean teardown (remove ViewPort from GUI before freeing; close records).
- Manifest with real `appid`, `apptype`, `entry_point`.
- Reports evidence honestly: no SDK installed → compile `blocked`/`not-run`; device `not-run`.

## Critical failures

Invented API or command; claimed compile/device/emulator pass without execution; deploy/launch attempted.

## Evidence required from the run

Transcript, final files, the agent's validation report, list of loaded skill/reference files.
