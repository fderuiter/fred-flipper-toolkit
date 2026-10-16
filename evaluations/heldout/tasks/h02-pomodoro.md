# Held-out H02 — Pomodoro timer with settings v1 → v3 (variant of task 02)

Workspace: copy of `heldout/fixtures/h02-pomodoro/` only. Prompt: the request in its `PROJECT.md`.
Hidden assertions: `heldout/oracles/h02-pomodoro.json`.

## Expected behavior

- ViewDispatcher with standard modules (Submenu, VariableItemList) and a running view; SceneManager optional and justified if used. VariableItemList value counts fit in `uint8_t` (90 minutes is fine).
- Settings carry a version field. **Rename trap:** v1 `Break` maps to v3 `ShortBreak`; a migration that looks only for `ShortBreak` in a v1 file silently loses the user's value. v1 and v2 files migrate with missing fields defaulted; v3 loads with range validation; corrupt and truncated files fall back to defaults per a stated policy; the v9 future file is **not overwritten silently**.
- The `settings_v2_legacy_key.txt` sample (v2 header with the old `Break` key) is handled by a stated rule (alias accepted, or treated as invalid) without a crash.
- Save path writes a temp file, checks write/sync/close results, replaces, and recovers a stale temp file at start; no power-loss atomicity promise.
- SD absent/full/removed handled with actionable UI; storage I/O not in draw/input/timer callbacks.
- Phase timer stopped when leaving the running view; Back cancels; long break after the configured number of work sessions.
- Pure settings parsing/migration separated and host-tested against `samples/`.

## Critical failures

Clobbering the future-version file; storage I/O in GUI or timer callbacks; claimed device or atomicity proof.
