---
name: flipper-verify
description: Build, test, SDK-migration and release evidence for Flipper Zero apps. Use when checking a FAP build, choosing or pinning an SDK, comparing API tables between firmware versions, planning host or device tests, packaging for the application catalog, or deciding what can be verified without hardware. Not for automatic publishing, unrelated CI changes, or ordinary design questions.
license: MIT
compatibility: Official Flipper Zero firmware 1.5.1, target f7, API 88.2 researched baseline. Requires sibling flipper-core directory for shared references.
metadata:
  suite: fred
  version: 0.1.0-preview
---

# FRED verify: build, migration and release evidence

## Purpose

Establish what is actually known to work, with exact identities, and prepare a supportable compatibility and release report.

## Activation

Use for:
- "Check that this FAP builds against 1.5.1."
- "Migrate my app to the new SDK."
- "What can we verify without a Flipper attached?"
- "Prepare this app for catalog submission."

Do not use for:
- publishing or submitting because something "looks good";
- unrelated CI/CD edits;
- design questions with no build/test/release element.

## Inputs and prerequisites

At entry, read the shared rules first, resolving paths relative to this skill directory:

1. [../flipper-core/references/engineering-rules.md](../flipper-core/references/engineering-rules.md)
2. [../flipper-core/references/evidence-policy.md](../flipper-core/references/evidence-policy.md)

If either file cannot be read, stop and report: "Broken installation: FRED shared references are missing. Install flipper-core, flipper-native, flipper-review and flipper-verify together as sibling directories." Do not continue from memory. This skill works without flipper-core having been invoked.

Also needed: target compatibility card, project source and build hooks, whether a trusted toolchain is already prepared, and the authorized action scope. Hardware is optional and must be explicitly scoped.

## Workflow

1. Inspect environment and execution effects: manifest, hooks, SDK state, whether a command would download or mutate anything.
2. Choose the smallest checks that answer the question.
3. Establish identities: bootstrap, SDK URL/SHA-256/API, toolchain, firmware commit.
4. Inspect manifest, imports and assets.
5. Run authorized checks (format, build, host tests) and keep exact commands, versions, exit codes and logs.
6. For migration, compare API tables, map changes to call sites, fill the impact table and rebuild plan below, then rebuild on the target.
7. Write a separate device test plan; never fill it from compile or mock results.
8. Prepare artifacts and evidence without publishing.

Decision points: if setup would download a toolchain or SDK, state the exact source, pin, effects and needed authority before doing it. If no trusted toolchain exists, mark compilation `blocked`, not `pass`.

## References

- [build-debug-release.md](../flipper-core/references/build-debug-release.md) — commands, isolation, migration, catalog.
- [compatibility.md](../flipper-core/references/compatibility.md) — baseline and support levels.
- [validation-report.md](../flipper-core/assets/validation-report.md) — evidence form.
- [device-test-plan.md](../flipper-core/assets/device-test-plan.md) — hardware plan form.
- [storage-parsers.md](../flipper-core/references/storage-parsers.md) — host corpora for parsers.

## Outputs and completion

- Check matrix: command, tool/SDK versions, status, exit code, log path.
- Artifact hashes and the exact compatibility actually tested.
- Pending device cases, listed explicitly.
- Release checklist and support notes when requested.

For any SDK or firmware migration, both of these are required outputs, even when no build was run:

| Impact | Evidence | Action |
|---|---|---|
| Source | changed or removed signatures and macros mapped to call sites | code edits; compile errors expected |
| Binary/import | old and new API major; every import checked against the target `api_symbols.csv` | rebuild; an old FAP is rejected on a major change, and a removed or unexported import fails at load even when the major matches [C-FAP-004] |
| Behavior | changed semantics, timing or defaults | device cases; `not-run` without hardware |

Rebuild plan: target SDK identity (channel or URL, SHA-256, API version), the exact `ufbt` build command, the import check (official build and loader, with `compare_api.py` as a cross-check), and the device cases it leaves pending.

Done when every evidence field has a status with a reason and nothing unexecuted is described as passed.

## Validation

The official compiler, linker and loader are the authority for builds and imports; the repository helper `scripts/compare_api.py` is supplementary for API tables. Lint is formatting evidence only. No device pass from compilation, mocks or emulators.

## Compatibility boundaries

Official SDK baseline. Other firmware labels are inspected, compiled and device-tested separately. No firmware flashing, deployment or appliance operation in an automatic check sequence.

## Failure modes

- Inventing commands such as `ufbt test`.
- Letting first-run bootstrap silently pick the moving release channel.
- Calling lint "static analysis".
- Reporting an old cached `dist/` artifact as a fresh build.
- Assuming catalog acceptance from YAML syntax.
- Collapsing compile, host and device results into one "verified".

## Handoffs

- flipper-native: fixes for build or behavior failures.
- flipper-review: crashes, lifecycle or concurrency concerns found during testing.
- flipper-core: migration changes required capabilities or lineage.
