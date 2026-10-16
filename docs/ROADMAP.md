# Implementation roadmap

Status (2026-10-16): v0.1 preview implemented — #2–#6 and #10 have their locally achievable work committed and offline checks passing; #7 (SDK), #8 (model comparison) and #9 (hardware) are pending external setup. Issue comments record commits and exact check results.

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

## Sequence and qualification boundaries

1. Establish compatibility/claim contracts, then skill entry points and shared references (#2–#4).
2. Add only the two bounded offline helpers and seven benchmark task/oracle sets (#5–#6).
3. Qualify actual SDK builds and host checks (#7), then measure model benefit under fixed conditions (#8).
4. Qualify hardware behavior separately (#9); operationalize maintenance/release policy (#10).
5. Expand only when repeated tasks and measured gaps justify it (#11).

No milestone is closed merely because documentation exists. SDK execution needs prepared or separately authorized setup (the implementation session had no toolchain and could not reach the SDK host); comparative evaluations need actual model sessions; device qualification needs explicit hardware scope. Existing local development authorization should not be repeatedly requested within its scope.

No custom MCP/server, installer, scaffolder, comprehensive emulator, protocol micro-skill collection or broad fork support is required for the first release. Those are deliberately deferred, not forgotten.

