# ADR 0003: Conditional token budget revision

Date: 2026-10-16. Status: accepted (user decision on #13). Revises the comparative token gate in `evaluations/rubric.json` (rubric version 2).

## Context

The research design set a budget of median treatment tokens ≤ 1.5× baseline for the pilot, "unless a documented correctness gain warrants an explicit budget revision". The two pilot rounds measured:

| Round | Median token ratio | Critical+major errors, baseline → FRED |
|---|---:|---|
| 1 | 1.79× | −58% |
| 2 | 1.75× | 45 → 21 (−53%) |

The per-file measurement in [`evaluations/results/2026-10-16-reference-load.md`](../../evaluations/results/2026-10-16-reference-load.md) shows where the overhead comes from:

- **Loading, not installation.** Sessions that load no skill files cost the same as baseline.
- **The shared core.** Rules, evidence policy and the entry SKILL.md stay in context for every later turn.
- **Extra turns.** Some go to loading the files and some to extra work, such as host tests and ownership notes.

Removing duplicated text projects 1.70×. Reaching 1.5× would mean dropping most of the always-loaded rules, which carry the guidance behind the error reduction.

## Decision

The comparative gate's token budget becomes **≤ 2.0× median**, but only when the same run also shows a correctness gain:

- at least 50% fewer predeclared critical and major errors than baseline;
- a treatment mean at or above the baseline mean.

Without that gain, the original 1.5× budget applies. `run_pilot.py report` applies the rule and reports `token_budget` and `token_ratio_within_budget`. It also keeps reporting the original `token_ratio_le_1_5`.

Scoring categories, weights and critical failures are unchanged, so scores stay comparable across versions. The revision applies to runs after 2026-10-16, starting with the held-out pilot (#12). Rounds 1 and 2 stay evaluated under rubric version 1.

No structural restructuring or rerun was done for this decision.

## Consequences

- Within the budget, FRED may spend up to twice the baseline's tokens for a measured halving of serious errors.
- If a later change loses the error reduction, the budget falls back to 1.5× automatically.
- The 2.0× ceiling leaves headroom for the guidance added for #21 and #22. That headroom is not a target: new guidance should still keep the always-loaded core small.
- The budget is checked again on the held-out pilot, the first run under rubric version 2.
