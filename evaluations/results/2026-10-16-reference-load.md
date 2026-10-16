# Reference loading and token cost — 2026-10-16 (issue #13)

**Result: deduplicating the topic references does not bring the median token ratio to the ≤ 1.5× budget.** Projected from the retained round 2 transcripts, the trim lowers the ratio from 1.75× to about 1.70×. No pilot was rerun, so the measured ratio is still 1.75×.

Method: `python3 evaluations/harness/measure_reference_load.py <round> --measured-at <skill revision>`. It reads the retained `session.json` records offline and launches nothing. Round 1 ran the skills at `920348f^` and round 2 at `920348f`.

- **Loaded files.** It finds the skills each treatment session invoked and every skill file read through `Read` or `Bash`.
- **Cost.** A loaded file stays in context, so its cost is estimated as size × turns remaining.
- **Size.** Estimated as characters ÷ 4.
- **Load turn.** Estimated from the event's position, because the records keep no turn numbers.

Cost figures are therefore estimates. The session totals and ratios are measured from the `usage` fields.

## Round 2 per-file cost (sizes at `920348f`)

| File | ≈ tokens | After trim | Sessions loading (of 21) | Median est. cost | Total est. cost |
|---|---:|---:|---:|---:|---:|
| `flipper-core/references/engineering-rules.md` | 1,813 | 1,813 | 13 | 16,317 | 266,511 |
| `flipper-native/SKILL.md` | 1,416 | 1,416 | 9 | 24,072 | 223,728 |
| `flipper-core/references/evidence-policy.md` | 767 | 667 | 13 | 6,903 | 106,613 |
| `flipper-core/references/runtime-ui.md` | 1,401 | 933 | 6 | 16,812 | 99,471 |
| `flipper-core/references/storage-parsers.md` | 936 | 847 | 7 | 14,976 | 96,408 |
| `flipper-core/SKILL.md` | 1,288 | 1,288 | 4 | 7,728 | 42,504 |
| `flipper-core/references/hardware-communications.md` | 987 | 951 | 4 | 10,363 | 36,519 |
| `flipper-review/SKILL.md` | 1,173 | 1,173 | 2 | 7,624 | 15,249 |
| `flipper-core/references/capability-map.md` | 1,191 | 1,191 | 3 | 3,573 | 10,719 |
| `flipper-core/references/compatibility.md` | 1,166 | 1,166 | 2 | 3,498 | 6,996 |
| `flipper-core/assets/ownership-table.md` | 243 | 243 | 2 | 2,187 | 4,374 |
| `flipper-core/assets/validation-report.md` | 193 | 193 | 1 | 1,930 | 1,930 |
| `flipper-core/assets/compatibility-card.md` | 295 | 295 | 2 | 885 | 1,770 |

`build-debug-release.md`, `upstream-examples.md` and `assumptions.md` were not loaded in round 2. Round 1 shows the same pattern; the command above reproduces its table.

| Round | Pairs where treatment loaded skill files | Median pair ratio | Median extra turns | Pairs with no skill load | Median pair ratio |
|---|---:|---:|---:|---:|---:|
| 1 (`920348f^`) | 17 | 1.75 | 5 | 4 | 1.01 |
| 2 (`920348f`) | 16 | 1.83 | 5 | 5 | 1.00 |

| Round | Median baseline | Median treatment | Ratio (measured) | Projected treatment after trim | Projected ratio |
|---|---:|---:|---:|---:|---:|
| 1 | 103,472 | 184,882 | 1.79 | 181,853 | 1.76 |
| 2 | 104,305 | 182,893 | 1.75 | 177,781 | 1.70 |

## Findings

1. **The overhead comes from loading skills, not from having them installed.** Treatment sessions that never invoked a skill or read a skill file cost the same as baseline (median pair ratio 1.00). The four descriptions in the system prompt are negligible.
2. **Every loading session pays for the shared core.** `engineering-rules.md`, `evidence-policy.md` and the entry SKILL.md add about 4,000 tokens. That content is then re-read from cache on every later turn. Across the 16 loading sessions in round 2, estimated content cost totals about 913,000 tokens, roughly 57,000 per session. The median per-session gap is 78,600, so content accounts for most of it.
3. **Extra turns account for the rest.**
   - A loading session takes a median of 5 more turns than its baseline pair. About 3.5 of those are loading tool calls: the Skill call, reading the shared rules, reading topic references, and in `flipper-core` sessions a `find .claude` to locate the files.
   - The remaining ~2 turns are extra work, such as writing host tests and ownership notes. Each turn re-reads a context of roughly 16,000 tokens.
4. **Selective loading already works.** `build-debug-release.md` ("only when building") was never loaded. Topic references were loaded only for matching tasks.
5. **What the budget needs.** Reaching 1.5× at round 2's median needs about 26,000 fewer treatment tokens. With about 10 turns remaining after loading, that means removing roughly 2,600 tokens of always-loaded content, which is most of `engineering-rules.md` plus `evidence-policy.md`, or removing about 1.5 turns per session.

## Trim made (topic references only)

Every entry point reads `engineering-rules.md` before any topic reference, so duplicated text was removed from the topic references rather than from the rules. The removed text:

- **`runtime-ui.md`:** the memory facts, context and timer stop/free facts, queue policy, logging and SceneManager-optional text that repeat rules §2–§6. The broken pointer to the repository's `examples/upstream-index.md` was replaced by a link to `upstream-examples.md`.
- **`storage-parsers.md`:** the "untrusted content" section, which repeats rules §10, and two tightened storage bullets.
- **`hardware-communications.md`:** the shared-peripheral and BLE peripheral text that repeats rules §8. GPIO electrical detail and the UART pattern stay.
- **`evidence-policy.md`:** the maintainer-facing claim-record schema and review-state definitions became one sentence. The full schema stays in `docs/architecture.md`.

Not changed:

- The teardown sections, because #22 is changing teardown guidance in parallel.
- The SKILL.md files and descriptions, because #21 and #22 edit them. With no description change, activation behaviour is not expected to change. Activation was not rerun.

Every claim keeps at least one citation. The claim consumers were refreshed.

## Options for the budget decision (not taken here)

- **Budget revision.** The rubric allows a documented revision when correctness gains justify it. Round 2 cut critical+major errors by 53% (45 → 21).
- **Structural changes.** These need a rerun to measure:
  - merge `evidence-policy.md` into a shorter `engineering-rules.md` so one read loads both;
  - give `flipper-core` exact relative paths so sessions stop running `find .claude`;
  - move the native workflow detail out of `flipper-native/SKILL.md`, which stays in context longest.
- **Measuring the effect.** Any of these is measured on the paired rerun planned for #12. Running it spends model sessions and needs the owner's approval.
