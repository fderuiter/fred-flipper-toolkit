# Evaluation plan and current status

Issue tracking: definitions [#6](https://github.com/fderuiter/fred-flipper-toolkit/issues/6), SDK qualification [#7](https://github.com/fderuiter/fred-flipper-toolkit/issues/7), controlled comparison [#8](https://github.com/fderuiter/fred-flipper-toolkit/issues/8), hardware [#9](https://github.com/fderuiter/fred-flipper-toolkit/issues/9).

## What exists

- Seven task definitions in `evaluations/tasks/`, synthetic fixtures in `evaluations/fixtures/`, reviewer-side oracles in `evaluations/oracles/` (including eight seeded lifecycle defects for task 06 and held-out variants).
- 24 activation cases (8 positive, 8 negative, 8 ambiguous/adversarial) in `evaluations/activation.json`, including fabricated `ufbt test`, invented `RECORD_PASSTHROUGH`, wrong lineage, stale `furi_alloc` example, missing SDK/hardware, prompt injection in fixture data, an overbroad NFC claim and an unscoped physical output.
- Predeclared rubric and gates in `evaluations/rubric.json`.

These were written before any evaluation of the skills. Tuning skill text against held-out variants is not allowed.

## Procedure (not yet executed)

1. Fix: model name/version, reasoning setting, client version, tools and permissions, time/token budget, SDK/cache state, hardware availability.
2. For each task and repetition (3), create two fresh workspaces containing only `fixtures/<task>/` and ordinary project facts. Treatment additionally installs the four `skills/` directories as siblings (see [project-instructions.md](project-instructions.md)). Baseline gets no toolkit text.
3. Randomize session order; run 42 sessions; retain everything per [results template](../evaluations/results/README.md).
4. Score from the reviewer side with oracles, blinded to arm where practical; apply critical-failure rules.
5. Activation: run each case in a fresh session; for non-core expected skills, verify direct entry without prior core load.
6. Evaluate gates from `rubric.json`; report misses, ceiling effects and limits honestly.

## Helper and content checks (FRED's own code)

Run from the repository root:

```sh
python3 scripts/check_bundle.py
python3 -m unittest discover -s tests -v
```

These validate the bundle and helpers. They are not evaluations of agent benefit, compilation, or device behavior. The latest recorded results are in the [changelog](../CHANGELOG.md) and the issue comments.

## Current status

| Gate | Status |
|---|---|
| Bundle metadata, links, budgets, direct-entry structure | pass (offline checks) |
| Helper unit/security tests | pass (offline) |
| Live activation in a named Claude Code version | not-run |
| SDK compile of fixtures, host tests of fixture logic | blocked (no prepared toolchain; SDK host unreachable from the implementation session) |
| Controlled 42-session pilot | not-run (needs model-session budget) |
| Device qualification | not-run (needs authorized hardware) |
