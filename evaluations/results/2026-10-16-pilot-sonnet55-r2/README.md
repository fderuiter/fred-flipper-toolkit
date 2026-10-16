# Pilot round 2 — 2026-10-16 (issue #8)

**Result: the comparative gate is still NOT met.** This round ran after the single improvement round allowed by the plan (commit `920348f`: generic, source-verified guidance). Everything else matched [round 1](../2026-10-16-pilot-sonnet55/README.md): `claude-sonnet-5-5`, Claude Code 2.1.301, the same tools, prompt and harness, and blind grading by `claude-opus-5-5`. Only the randomization seed was new (20261017). All 42 sessions were rerun fresh, baseline included.

| Task | Baseline mean | Treatment mean | Δ | Baseline crit/major | Treatment crit/major |
|---|---|---|---|---|---|
| 01 utility | 90.7 | 90.2 | −0.5 | 0 / 4 | 0 / 2 |
| 02 settings | 58.7 | 81.7 | +23.0 | 2 / 20 | 0 / 8 |
| 03 UART | 72.0 | 80.7 | +8.7 | 0 / 7 | 1 / 5 |
| 04 `.ir` parser | 94.7 | 94.3 | −0.4 | 0 / 0 | 0 / 0 |
| 05 BLE feasibility | 61.7 | 92.2 | +30.5 | 0 / 5 | 0 / 0 |
| 06 lifecycle review | 88.0 | 93.5 | +5.5 | 0 / 6 | 0 / 2 |
| 07 SDK migration | 74.8 | 74.3 | −0.5 | 0 / 1 | 0 / 3 |
| **All** | **77.2** | **86.7** | **+9.5** | 45 critical+major | 21 |

| Gate | Round 1 | Round 2 |
|---|---|---|
| Treatment mean ≥ 85 | fail (84.6) | **pass (86.7)** |
| No task family < 80 | fail (02: 74.0) | **fail (07: 74.3)** |
| No treatment critical failure | pass | **fail (1)** |
| Paired improvement ≥ 10 | fail (+7.2) | fail (+9.5) |
| *or* ≥ 50% fewer critical + major errors | pass (−58%) | pass (−53%) |
| Median token ratio ≤ 1.5× | fail (1.79×) | fail (1.75×) |

**Treatment critical (03 UART, score 73).** The session freed the ViewPort before stopping and joining the worker thread, which still calls `view_port_update`. That is a use-after-free during teardown, the exact pattern the references warn about. It counts as a critical failure under the predeclared rule.

**Baseline criticals (two in 02 settings).** Both sessions silently overwrote the future-version settings file.

**Migration (07) did not improve.** Treatment answers still under-reported binary/import impact, even after the guidance was added.

Isolation: six sessions are flagged because they wrote test binaries into their own Claude Code scratchpad under `/tmp/claude-0/`. No session accessed the repository or the oracles. Cost: $9.36 for sessions; grading costs are in each `grade.json`.

**Interpretation.** FRED helps most on feasibility, settings/persistence and UART integration, and it cut errors roughly in half. It does not yet meet the predeclared gate, so no effectiveness claim beyond these measured numbers is made. The plan does not allow a further tuning round on these training tasks. Each missed gate has its own issue: migration family [#21](https://github.com/fderuiter/fred-flipper-toolkit/issues/21), treatment critical [#22](https://github.com/fderuiter/fred-flipper-toolkit/issues/22), paired gain [#23](https://github.com/fderuiter/fred-flipper-toolkit/issues/23), token ratio [#13](https://github.com/fderuiter/fred-flipper-toolkit/issues/13). Related follow-up work is tracked in [#12](https://github.com/fderuiter/fred-flipper-toolkit/issues/12) (held-out validation), [#13](https://github.com/fderuiter/fred-flipper-toolkit/issues/13) (token budget), [#14](https://github.com/fderuiter/fred-flipper-toolkit/issues/14) (sandboxed host tests) and [#21](https://github.com/fderuiter/fred-flipper-toolkit/issues/21) (migration and teardown weaknesses).
