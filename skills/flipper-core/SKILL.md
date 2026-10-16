---
name: flipper-core
description: Flipper Zero feasibility, compatibility and capability checks. Use when deciding whether something can run on a Flipper Zero or which surface fits (native FAP, JavaScript, built-in app, firmware change, host companion, external microcontroller); when porting code or examples from Momentum, Unleashed or another firmware fork; when checking whether a Flipper API, record or command really exists; or before wiring, powering or driving GPIO pins or radios. Not for routine edits to an already-scoped FAP, generic C questions, or prose that merely mentions Flipper.
license: MIT
compatibility: Researched against official Flipper Zero firmware 1.5.1, target f7, API 88.2. Install with flipper-native, flipper-review and flipper-verify as sibling directories.
metadata:
  suite: fred
  version: 0.1.0-preview
---

# FRED core: feasibility and compatibility

## Purpose

Choose a feasible execution surface for a Flipper Zero goal and produce a compatibility-grounded plan with explicit capability gaps. An evidence-backed "unsupported on this surface, use X instead" is a complete result.

## Activation

Use for:
- "Can a stock Flipper control my BLE coffee machine?"
- "Should this be a FAP or a JS script?"
- "Plan a Flipper project that logs a UART sensor to SD."
- "Is `furi_hal_xyz` available to external apps on 1.5.1?"

Do not use for:
- a blog/prose edit that mentions Flipper in passing;
- a generic C or Python question;
- operating an already configured remote or device;
- a direct code change to a known, already-scoped FAP (use flipper-native).

## Inputs and prerequisites

User goal and expected interactions; the user's repository or files if any; authorized action scope. No hardware required. Resolve target facts from the repository before asking; ask only about unknowns that block the decision.

## Workflow

1. Read [references/engineering-rules.md](references/engineering-rules.md) and [references/evidence-policy.md](references/evidence-policy.md). If either is missing, report a broken FRED installation and stop.
2. Classify the request: feasibility, planning, API question, or out of scope.
3. Establish compatibility with [references/compatibility.md](references/compatibility.md) and the [compatibility card](assets/compatibility-card.md). Keep unknowns explicit.
4. List the required operations (roles, transports, timing, storage, UI) from the real target, not the product name.
5. For each operation trace header → implementation → export/library → owner on the baseline. Use [references/capability-map.md](references/capability-map.md).
6. Compare native FAP, JS, built-in, firmware, host and companion MCU by those operations.
7. Decide: chosen surface, rejected surfaces with reasons, unknowns, external effects, validation plan.
8. Hand off only the work that is needed.

Decision points: if a required operation is not exported, do not design around invented APIs; recommend the surface that has it. If the lineage is a fork, inspect that fork or mark it unresolved.

## References

Load on demand, relative to this skill directory:
- [references/compatibility.md](references/compatibility.md) — baseline, three kinds of compatibility, support levels.
- [references/capability-map.md](references/capability-map.md) — surfaces, domains, BLE stress test.
- [references/hardware-communications.md](references/hardware-communications.md) — physical interfaces.
- [references/assumptions.md](references/assumptions.md) — when a common assumption is challenged.
- [references/upstream-examples.md](references/upstream-examples.md) — pinned official examples.
- [references/sources.json](references/sources.json) — claim registry.

## Outputs and completion

- Compatibility card (filled, unknowns marked).
- Capability decision citing interfaces and claim IDs or pinned source paths.
- Minimal plan for the chosen surface; rejected options with reasons.
- Material unknowns, external effects needing authority, validation plan.

Done when the user can act on the decision and every API-sensitive statement has a source or an "unresolved" label.

## Validation

Cross-check key APIs against the pinned `api_symbols.csv`; compare alternatives by required operations; label board, radio-stack and electrical claims that were not verified. Never claim compile or device results here.

## Compatibility boundaries

Official firmware 1.5.1 / f7 / API 88.2 researched baseline. Other lineages are provisional until inspected. JS, firmware, host and MCU guidance is feasibility-level only.

## Failure modes

- Treating silicon capability as public SDK capability.
- "BLE is supported" without naming the role.
- Asking questions the repository already answers.
- Refusing legitimate RF/NFC/IR development wholesale.
- Generating app code for an infeasible surface.

## Handoffs

- flipper-native: receives the compatibility card and capability plan for implementation.
- flipper-review: receives constraints for lifecycle/concurrency review.
- flipper-verify: receives the required evidence list.

Handoffs are workflow steps, not mandatory subagents. Do not load every skill for every task.
