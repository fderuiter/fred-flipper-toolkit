# Pilot round 1 — 2026-10-16 (issue #8)

**Result: the comparative gate is NOT met.** The treatment arm (FRED installed) scored higher than the baseline and had fewer errors, but it missed the predeclared mean, task-family and token thresholds.

## Conditions

- **Sessions:** 42 fresh headless Claude Code 2.1.301 sessions: 7 tasks × 3 repetitions × 2 arms, in randomized order (seed 20261016; see `run.json`).
- **Model:** `claude-sonnet-5-5`, effort medium, $3 cap per session.
- **Prompt:** identical in both arms: "Read PROJECT.md in the current directory and complete the request it describes."
- **Workspace:** a fresh copy of `fixtures/<task>/` only. The treatment arm also had the four FRED skills installed at project scope; the baseline had none. Both arms had the account's other ~110 installed skills listed.
- **Tools:** Read, Glob, Grep, Write, Edit, Skill, and Bash limited by allow rules to `gcc`/`cc`/`clang`/`make`/`./…`/`ls`/`python3 -c`/`xxd`/`od`. Anything else was denied without a prompt. Most compound shell commands were therefore denied in both arms, so many sessions could not run their own host tests. That limit was identical for both arms.
- **Isolation:** deny rules covered the repository and other private paths. No session accessed the repository, oracles or research. The seven sessions flagged in `summary.json` wrote test binaries into their own Claude Code scratchpad under `/tmp/claude-0/`; none read private data.
- **Grading:** each session was scored by a separate `claude-opus-5-5` session against the predeclared oracle and rubric, with no tools and no arm label. Session IDs are opaque hashes. This is LLM grading, not human review; per-assertion rationales are in `sessions/*/grade.json`.

## Scores (0–100)

| Task | Baseline mean | Treatment mean | Baseline crit/major | Treatment crit/major |
|---|---|---|---|---|
| 01 utility | 90.5 | 87.3 | 0 / 2 | 0 / 0 |
| 02 settings | 66.0 | 74.0 | 1 / 18 | 0 / 13 |
| 03 UART | 71.0 | 88.3 | 1 / 9 | 0 / 1 |
| 04 `.ir` parser | 92.2 | 82.2 | 0 / 0 | 0 / 3 |
| 05 BLE feasibility | 62.7 | 88.0 | 0 / 2 | 0 / 0 |
| 06 lifecycle review | 86.2 | 96.0 | 0 / 7 | 0 / 0 |
| 07 SDK migration | 73.0 | 76.3 | 0 / 5 | 0 / 2 |
| **All** | **77.4** | **84.6** | 2 sessions / 43 | 0 / 19 |

## Gates (`rubric.json`)

| Gate | Result |
|---|---|
| Treatment mean ≥ 85 | **fail** (84.6) |
| No task family mean < 80 | **fail** (02: 74.0, 07: 76.3) |
| No critical failure in treatment | pass (0; baseline had 2) |
| Paired mean improvement ≥ 10 | fail (+7.2) |
| *or* ≥ 50% fewer critical + major errors | pass (45 → 19, −58%) |
| Median token ratio ≤ 1.5× | **fail** (1.79×; 184,882 vs 103,472) |
| Activation ≥ 23/24, 0 negative false activations | pass (separate runs) |

Total session cost: $9.38. Grading cost is in each `grade.json`.

## What the grades show

- **Largest gains:** feasibility (05, +25), UART (03, +17) and lifecycle review (06, +10, with 7 baseline major errors reduced to 0).
- **Treatment below baseline:**
  - On 04, one treatment session shipped an off-by-one header check (`"IR signals file"` compared with length 14), so its parser rejected every valid file. The shell restriction kept it from running its own host tests.
  - On 01, treatment sessions mutated state in the GUI input callback instead of handing input to the app thread, and their build instructions did not pin the SDK.
- **Recurring treatment errors in 02:**
  - `VariableItemList` value counts above 255 (the count and index are `uint8_t`);
  - blocking storage I/O in a View `previous_callback`;
  - no modeled interrupted-write test.
- **07:** answers explained the compile breaks but left out binary/import compatibility (old FAP rejected on the major change; removed exports fail at load).

Under the predeclared process, these findings feed one improvement round on the training tasks. Held-out task variants must be checked before any stable release.
