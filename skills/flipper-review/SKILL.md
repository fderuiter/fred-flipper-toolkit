---
name: flipper-review
description: Review or debug Flipper Zero native app code for lifecycle, ownership, concurrency, callback-context, parser and API-boundary defects. Use for FAP code reviews, crashes after exit or mode changes, races, leaks, queue overflows, timer or worker teardown problems, and bounds checks in file or protocol parsers. Not for build-command questions, marketing copy, or operating hardware.
license: MIT
compatibility: Official Flipper Zero firmware 1.5.1, target f7, API 88.2 researched baseline. Requires sibling flipper-core directory for shared references.
metadata:
  suite: fred
  version: 0.1.0-preview
---

# FRED review: lifecycle, concurrency and parser review

## Purpose

Find concrete defects with causal traces and debug from evidence rather than guesses. Each finding says what triggers it, what happens, how sure we are, and how to fix and test it.

## Activation

Use for:
- "Review this FAP for races and leaks."
- "The app crashes after leaving UART mode."
- "Is this `.ir` parser bounds-safe?"
- "Why does exit hang when the worker is busy?"

Do not use for:
- "Which ufbt command builds my app?" (flipper-verify);
- prose or marketing text;
- requests to operate a device or transmit.

## Inputs and prerequisites

At entry, read the shared rules first, resolving paths relative to this skill directory:

1. [../flipper-core/references/engineering-rules.md](../flipper-core/references/engineering-rules.md)
2. [../flipper-core/references/evidence-policy.md](../flipper-core/references/evidence-policy.md)

If either file cannot be read, stop and report: "Broken installation: FRED shared references are missing. Install flipper-core, flipper-native, flipper-review and flipper-verify together as sibling directories." Do not continue from memory. This skill works without flipper-core having been invoked.

Also needed: the code or diff and its context; crash text, logs and firmware/SDK identity when available. A missing reproduction is an unknown, not a reason to invent one.

## Workflow

1. Trace the public/import boundary: every API used is exported at the target or comes from a declared library.
2. Build the resource graph: owners, borrowers, producers (timers, IRQ callbacks, threads, GUI callbacks) and what each touches.
3. Annotate contexts and locks: GUI, dispatcher, timer daemon, IRQ, worker; lock order and waits under locks.
4. Walk error, partial-init and teardown paths: does every producer stop and quiesce before shared state is freed?
5. Check queue/stream saturation, cancellation and wakeups.
6. Check parser and size arithmetic, short storage I/O and SD failures.
7. Write findings; propose minimal fixes that do not deadlock shutdown.

Review-only requests stay read-only: no edits unless the user asks for fixes.

## References

- [runtime-ui.md](../flipper-core/references/runtime-ui.md) — contexts, timers, records, teardown, UI.
- [storage-parsers.md](../flipper-core/references/storage-parsers.md) — parsers, settings, storage.
- [hardware-communications.md](../flipper-core/references/hardware-communications.md) — UART/bus paths.
- [assumptions.md](../flipper-core/references/assumptions.md) — avoid false positives from folklore.
- [build-debug-release.md](../flipper-core/references/build-debug-release.md) — crash evidence collection.

## Outputs and completion

Severity-ranked findings (critical, major, minor), each with:
- file and function;
- trigger or interleaving;
- consequence;
- evidence level: source trace, reproduced, or hypothesis;
- specific fix and the test that would show it.

Include a teardown table whenever the code has producers: each producer, what it touches, and whether it is stopped (and joined) before those are freed. A failing row is a finding.

Also state the inspected and uninspected scope and residual uncertainty. "No findings" requires that scope statement.

## Validation

Reproduce statically by an interleaving or lifetime trace, or with authorized tests. Do not upgrade a plausible hypothesis to a proven crash. Check that proposed fixes do not join under a needed lock, flush from the timer daemon, or drop exit events.

## Compatibility boundaries

Official FAPs at the researched baseline. Fork internals need separate evidence. Not an electrical, RF-compliance or security certification.

## Failure modes

- Generic checklist dumps without traces.
- Flagging `malloc` or blocking worker loops by rule rather than by context.
- "Reverse the frees" without proving callbacks stopped.
- Treating `furi_timer_stop` as a callback barrier.
- Guessing stack sizes instead of collecting crash evidence.
- Inventing APIs in proposed fixes.

## Handoffs

- flipper-native: implement authorized fixes.
- flipper-verify: turn fixes into repeatable checks and device test cases.
- flipper-core: the code depends on an unsupported subsystem or role.
