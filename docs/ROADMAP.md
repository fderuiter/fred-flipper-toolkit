# Implementation roadmap

Status (2026-10-16): v0.1 preview implemented — #2–#7 and #10 are complete with recorded evidence; #8 (model comparison) and #9 (hardware) remain, plus follow-ups #12–#23. Issue comments record commits and exact check results.

Tracker: [#1](https://github.com/fderuiter/fred-flipper-toolkit/issues/1). The [implementation prompt](implementation-prompt.md) gives concrete files, sequence, sources and release gates. The [full report](research/2026-10-16-research-design.md) explains the decisions.

| Order | Issue | Priority | Depends on | Completion evidence |
|---|---|---|---|---|
| M0 / v0.1 | [#2: Implement compatibility and claim-evidence registry contracts](https://github.com/fderuiter/fred-flipper-toolkit/issues/2) | P0 | Research baseline | Implementation/content checks and evidence linked to issue |
| M1 / v0.1 | [#3: Implement the four-skill bundle and direct-entry routing](https://github.com/fderuiter/fred-flipper-toolkit/issues/3) | P0 | #2 | Implementation/content checks and evidence linked to issue |
| M2 / v0.1 | [#4: Write bounded source-backed engineering and capability references](https://github.com/fderuiter/fred-flipper-toolkit/issues/4) | P0 | #2, #3 | Implementation/content checks and evidence linked to issue |
| M3 / v0.1 | [#5: Implement offline bundle-integrity and SDK API comparison helpers](https://github.com/fderuiter/fred-flipper-toolkit/issues/5) | P1 | #2, #3, #4 | Implementation/content checks and evidence linked to issue |
| M4 / v0.1 | [#6: Create seven benchmark tasks, 24 activation cases, and objective oracles](https://github.com/fderuiter/fred-flipper-toolkit/issues/6) | P1 | #3, #4, #5 | Implementation/content checks and evidence linked to issue |
| M5 / SDK qualification | [#7: Qualify designated fixtures against official 1.5.1 f7 SDK](https://github.com/fderuiter/fred-flipper-toolkit/issues/7) | P1 | #5, #6 | Actual SDK byte verification, builds and host tests |
| M6 / v0.1 effectiveness | [#8: Measure toolkit benefit with a controlled paired agent evaluation](https://github.com/fderuiter/fred-flipper-toolkit/issues/8) | P1 | #6, #7 | Controlled 42-session pilot and activation/context results |
| M7 / stable native support | [#9: Qualify native UI, settings recovery, and UART behavior on real hardware](https://github.com/fderuiter/fred-flipper-toolkit/issues/9) | P1 | #7 | Authorized real-device observations |
| v0.1 release governance | [#10: Operationalize maintenance, contribution, and evidence-based release policy](https://github.com/fderuiter/fred-flipper-toolkit/issues/10) | P1 | #2, #3, #4 | Implementation/content checks and evidence linked to issue |
| Research backlog / expansion-gated | [#11: Resolve evidence gaps before optional JS, fork, simulator, or device-adapter expansion](https://github.com/fderuiter/fred-flipper-toolkit/issues/11) | P2 | #2 | Source-backed expansion decisions; no inferred support |

## Follow-up work (opened 2026-10-16)

| Issue | Priority | Depends on | Completion evidence |
|---|---|---|---|
| [#12: Build held-out benchmark fixtures and run held-out validation](https://github.com/fderuiter/fred-flipper-toolkit/issues/12) | P1 | #8 | Held-out fixtures/oracles, reference builds, paired held-out run |
| [#13: Reduce skill context overhead to the 1.5x token budget](https://github.com/fderuiter/fred-flipper-toolkit/issues/13) | P1 | — | Token cost table; ratio ≤ 1.5× or documented budget revision. Table and trim done (#27); budget revised in rubric v2 ([ADR 0003](adr/0003-token-budget-revision.md)); activation recheck pending |
| [#14: Pilot harness: let both arms compile and run host tests inside an isolated sandbox](https://github.com/fderuiter/fred-flipper-toolkit/issues/14) | P2 | — | Sandbox proof and paired smoke run. Proof done offline ([record](../evaluations/results/2026-10-16-sandbox-proof/README.md)); smoke run needs approved model spend |
| [#15: Test the setup wizard on Windows and macOS](https://github.com/fderuiter/fred-flipper-toolkit/issues/15) | P1 | — | Per-OS results; tests pass |
| [#16: Resolve remaining GPIO electrical unknowns](https://github.com/fderuiter/fred-flipper-toolkit/issues/16) | P2 | — | AN4899, schematic and ESD sources or recorded unavailability |
| [#17: Investigate .fap build reproducibility](https://github.com/fderuiter/fred-flipper-toolkit/issues/17) | P3 | — | Section-level root cause; recipe or documented limit |
| [#18: Qualify FRED in other agent clients](https://github.com/fderuiter/fred-flipper-toolkit/issues/18) | P3 | demand | Per-client activation results |
| [#19: First maintenance review (due 2026-11-16)](https://github.com/fderuiter/fred-flipper-toolkit/issues/19) | P2 | — | Review checklist with check results |
| [#21: Pilot gap — SDK-migration family 74.3 < 80](https://github.com/fderuiter/fred-flipper-toolkit/issues/21) | P1 | #12 | Migration family ≥ 80 on a held-out paired rerun |
| [#22: Pilot gap — critical teardown bug in a FRED session](https://github.com/fderuiter/fred-flipper-toolkit/issues/22) | P1 | #12 | Zero treatment critical failures in the next paired run |
| [#23: Pilot gap — paired improvement +9.5 < +10](https://github.com/fderuiter/fred-flipper-toolkit/issues/23) | P2 | #21, #22 | Held-out paired gain with uncertainty; gate decision on #8 |
| [#20: Cut the v0.1.0-preview release](https://github.com/fderuiter/fred-flipper-toolkit/issues/20) | P1 | — | Done: tag `v0.1.0-preview` (`a85490a`), [GitHub pre-release](https://github.com/fderuiter/fred-flipper-toolkit/releases/tag/v0.1.0-preview), [release notes](releases/v0.1.0-preview.md) |

## Sequence and qualification boundaries

1. Establish compatibility/claim contracts, then skill entry points and shared references (#2–#4).
2. Add only the two bounded offline helpers and seven benchmark task/oracle sets (#5–#6).
3. Qualify actual SDK builds and host checks (#7), then measure model benefit under fixed conditions (#8).
4. Qualify hardware behavior separately (#9); operationalize maintenance/release policy (#10).
5. Expand only when repeated tasks and measured gaps justify it (#11).

No milestone is closed merely because documentation exists. SDK execution needs prepared or separately authorized setup (the implementation session had no toolchain and could not reach the SDK host); comparative evaluations need actual model sessions; device qualification needs explicit hardware scope. Existing local development authorization should not be repeatedly requested within its scope.

No custom MCP/server, installer, scaffolder, comprehensive emulator, protocol micro-skill collection or broad fork support is required for the first release. Those are deliberately deferred, not forgotten.

