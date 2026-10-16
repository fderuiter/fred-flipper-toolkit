# Live activation results — 2026-10-16

Harness: `evaluations/harness/run_activation.py` (reviewer-side). Client: Claude Code 2.1.301, headless `claude -p`, effort `medium`, tools Read/Glob/Grep/Skill only, deny rules for the repository and private paths, `--strict-mcp-config`, no session persistence. Each case ran in a fresh workspace with 114 skills listed (the four FRED skills plus the account's other installed skills). Cases: the predeclared 24 in `evaluations/activation.json` and the 6 held-out paraphrases in `evaluations/oracles/heldout.json`.

| Run | Model | Scope | Pass | Negative false activations | Direct entry | Private-path access | Cost (USD) |
|---|---|---|---|---|---|---|---|
| `…-sonnet55-project` (before tuning) | claude-sonnet-5-5 | project | 20/24 | 0/8 | all pass | none¹ | 2.04 |
| `…-sonnet55-project-r2` | claude-sonnet-5-5 | project | 23/24 | 0/8 | all pass | none | 1.99 |
| `…-sonnet55-project-r2b` (repeat) | claude-sonnet-5-5 | project | 23/24 | 0/8 | all pass | none | 2.02 |
| `…-sonnet55-personal-r2` | claude-sonnet-5-5 | personal, unrelated cwd | 23/24 | 0/8 | all pass | none | 1.58 |
| `…-opus55-project-r2` | claude-opus-5-5 | project | 23/24 | 0/8 | all pass | none | 4.57 |
| `…-sonnet55-heldout-r2` | claude-sonnet-5-5 | project | 6/6 | 0/2 | all pass | none | 0.52 |
| `…-opus55-heldout-r2` | claude-opus-5-5 | project | 6/6 | 0/2 | all pass | none | 1.28 |
| `…-sonnet55-project-r3` (after #24, #25, #27 at `5cc7722`) | claude-sonnet-5-5 | project | 23/24 | 0/8 | 9/10 (A06 missed)² | none | 2.09 |
| `…-sonnet55-a06-repeats-r3` (A06 × 3) | claude-sonnet-5-5 | project | 0/3 | n/a | n/a (no skill loaded) | none | 0.18 |

¹ The first run's summary flags N04 and P08 under the older audit, which counted any path outside the workspace. N04 attempted a write to a hallucinated sibling path (denied: Write was not an available tool); P08 grepped its own per-case sandbox directory. Neither touched the repository or another private path. The audit now separates private-path access from sandbox access.

² In r3, A06 loaded `flipper-native` but did not read the shared rules before asking for the missing capture file. In three repeats it loaded no skill: each time it asked for the file first. No description has changed since tuning, so this is treated as variance on a case whose fixture is absent. Tracked in [#32](https://github.com/fderuiter/fred-flipper-toolkit/issues/32).

**Tuning (one round, as the plan allows):** after the first run, the `flipper-core` description gained porting from forks, "does this API/record/command exist" and wiring/driving GPIO; `flipper-native` gained Furi records/services and capture/data-file parsers. The held-out paraphrases were not used for tuning.

**Direct entry:** in every session that loaded `flipper-native`, `flipper-review` or `flipper-verify`, the session read `../flipper-core/references/engineering-rules.md` and `evidence-policy.md` itself; `flipper-core` was never needed first.

**Remaining miss:** A02 ("Use RECORD_PASSTHROUGH…") expects `flipper-native`. Sonnet answered correctly without loading any skill (it found claim C-REC-002 by searching the installed references); Opus loaded `flipper-core`. Both correctly refused to invent the macro. Recorded as a failure under the predeclared rule; no further tuning.

**Gate:** ≥23/24 with zero negative false activations. Met in all five post-tuning full runs, across two models and both install scopes, including r3 after the #13 trim. A06 is unstable on repeats (#32), so the margin is thin. Limits: one repetition per configuration (two for sonnet project scope); single client version; activation measured with read-only tools.

**Not supported:** `claude --restricted` does not load project-scope skills, so restricted mode cannot be used with FRED installed at project scope.
