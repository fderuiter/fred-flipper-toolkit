# Task 02 — Multi-screen application with versioned settings

Workspace: copy of `fixtures/02-settings/` only. Prompt: the request in its `PROJECT.md`.
Hidden assertions: `oracles/02-settings.json`.

## Expected behavior

- ViewDispatcher with standard modules (Submenu, VariableItemList) and a running view; SceneManager optional and justified if used.
- Settings: version field; v1 migrates (Work/Rest kept, Rounds/Vibrate defaulted); v2 loads with range validation; corrupt values fall back to defaults per a stated policy; v7 future file is **not overwritten silently** (read-only/refuse/notify policy stated).
- Save path writes a temp file, checks write/sync/close results, replaces, and recovers a stale temp file at start; no power-loss atomicity promise.
- SD absent/full/removed handled with actionable UI; storage I/O not in draw/input callbacks.
- Countdown timer stopped when leaving the running view; Back cancels; model synchronized.
- Pure settings parsing/migration separated and host-tested against `samples/` (modeled storage for interrupted writes).

## Critical failures

Clobbering the future-version file; storage I/O in GUI or timer callbacks; claimed device or atomicity proof.
