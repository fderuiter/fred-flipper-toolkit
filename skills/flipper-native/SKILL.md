---
name: flipper-native
description: Implement or change native Flipper Zero external apps (FAPs) in C on official firmware. Use when adding screens, settings, storage, timers, worker threads, UART or bus handling to a FAP, using Furi records or services from app code, writing parsers for capture or data files a FAP reads, or fixing a ViewPort, View or ViewDispatcher implementation. Not for feasibility-only questions, firmware or radio-stack rewrites, host applications, or ordinary JavaScript.
license: MIT
compatibility: Official Flipper Zero firmware 1.5.1, target f7, API 88.2 researched baseline. Requires sibling flipper-core directory for shared references.
metadata:
  suite: fred
  version: 0.1.0-preview
---

# FRED native: FAP implementation

## Purpose

Make the requested native FAP change with an appropriate UI/state architecture, explicit ownership and callback contexts, bounded I/O, and an honest evidence report.

## Activation

Use for:
- "Add a settings screen with persisted values to this FAP."
- "Implement bounded UART RX with resync in my app."
- "My view model flickers and sometimes crashes; fix it."
- "Add a long-press reset to this counter app."

Do not use for:
- "Could a Flipper do X?" without a request to build (use flipper-core);
- firmware service, CPU2 or radio-stack changes;
- a standalone desktop/phone app;
- JavaScript unrelated to Flipper.

## Inputs and prerequisites

At entry, read the shared rules first, resolving paths relative to this skill directory:

1. [../flipper-core/references/engineering-rules.md](../flipper-core/references/engineering-rules.md)
2. [../flipper-core/references/evidence-policy.md](../flipper-core/references/evidence-policy.md)

If either file cannot be read, stop and report: "Broken installation: FRED shared references are missing. Install flipper-core, flipper-native, flipper-review and flipper-verify together as sibling directories." Do not continue from memory. This skill works without flipper-core having been invoked.

Also needed: a FAP workspace (or an explicit request to create one), the target firmware/SDK or an explicit provisional baseline, and the authorized scope. If a required capability is uncertain, do the flipper-core capability check first.

## Workflow

1. Inspect `application.fam`, build hooks, sources and existing patterns. Do not run builds before reading hooks.
2. Confirm every API you will use is exported at the target (`api_symbols.csv`) or comes from a declared library.
3. Choose the smallest UI/state architecture (ViewPort, View/modules, ViewDispatcher, optional SceneManager).
4. For nontrivial resources fill the [ownership table](../flipper-core/assets/ownership-table.md): owner, producers, context, failure behavior, stop/join/drain, release.
5. Separate bounded pure logic (parsers, framing, state machines) from Furi I/O so it can be host-tested.
6. Implement the smallest complete slice: init with partial-failure cleanup, run loop with cancel, teardown in dependency order.
7. Review your own diff for context violations, full queues, short I/O, overflow and use-after-free.
8. **Teardown self-check (mandatory, before completion).** Write a table, one row per producer (timer, IRQ/RX callback, worker thread, GUI/input/draw callback): what it touches (ViewPort, view model, buffers, queues, mutex, HAL handle) and the teardown line that stops it (and joins or frees it). Confirm in the code that each row's stop/join line runs before anything it touches is freed, removed or released; for example, a worker calling `view_port_update` is joined before `view_port_free`. Fix any row that fails before finishing.
9. Run host tests for extracted logic when the environment allows (otherwise give the exact command), run other authorized checks (see flipper-verify) and report evidence. Give pinned build instructions (SDK tag or verified URL, isolated `UFBT_HOME`).

Decision points: if the request needs an unexported capability, stop and explain rather than reaching into internals. If the task drifts into firmware or host work, say so instead of silently converting it.

## References

- [runtime-ui.md](../flipper-core/references/runtime-ui.md) — always for lifecycle/UI work.
- [storage-parsers.md](../flipper-core/references/storage-parsers.md) — files, formats, settings.
- [hardware-communications.md](../flipper-core/references/hardware-communications.md) — UART, buses, GPIO.
- [build-debug-release.md](../flipper-core/references/build-debug-release.md) — only when building.
- [upstream-examples.md](../flipper-core/references/upstream-examples.md) — pinned official examples.

## Outputs and completion

- The requested code/manifest/asset changes (or a concrete plan if only planning was asked).
- Ownership notes for nontrivial resources and the teardown self-check table (step 8).
- Host tests for separable logic and failure cases where practical.
- Evidence report: what was inspected, built or tested, and what is pending.

Done when the change is implemented, its exit path and failure paths are handled, and evidence is reported without overstatement.

## Validation

Imports resolve against the target exports; manifest `appid`, `apptype` and `entry_point` are correct; host tests pass for extracted logic; compilation against the pinned SDK when available; device behavior stays pending unless actually tested.

## Compatibility boundaries

Official native FAPs at the researched baseline. Does not modify firmware, take over shared hardware globally, or claim fork support.

## Failure modes

- Mandatory SceneManager for a one-screen app.
- Storage or parsing inside draw/input or View previous callbacks.
- `VariableItemList` with more than 255 values (counts are `uint8_t`).
- Unbounded parser or allocation; `count × size` overflow.
- Dropped Back/cancel event on a full queue.
- "Check malloc for NULL" presented as OOM handling.
- Freeing a timer context or buffer while a callback can still run.
- Freeing the ViewPort or view model before the worker that updates it is joined.
- Logging or allocating in IRQ callbacks.

## Handoffs

- flipper-core: missing or uncertain capability.
- flipper-review: nontrivial lifecycle, concurrency or parser changes deserve a review pass.
- flipper-verify: builds, SDK migration, artifacts and release evidence.
