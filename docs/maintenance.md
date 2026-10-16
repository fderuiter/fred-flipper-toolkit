# Maintenance and release policy

Owner: the repository maintainer (currently @fderuiter) is accountable for reviews and releases. No scheduler, updater or hosted CI is required; every step below can run locally.

## Review cadence

Review **monthly** and **whenever a new official firmware release or API version appears**. A review compares, at the new pin versus the baseline pin:

- release notes and `targets/f7/api_symbols.csv` (run `python3 scripts/compare_api.py --old OLD --new NEW` on two locally saved tables);
- headers and implementations behind each registry claim: `furi/core/{memmgr,memmgr_heap,record,message_queue,timer}.c`, GUI headers, storage API, serial HAL, `targets/f7/ble_glue/gap.c`;
- FAP loader/manifest code, uFBT and SDK scripts (`scripts/ufbt/SConstruct`, `scripts/lint.py`), JS docs/modules;
- the application catalog's guide, `AGENTS.md` and `tools/bundle.py`;
- Claude Code skill packaging docs and the Agent Skills specification.

## Claim lifecycle

Each claim in `skills/flipper-core/references/sources.json` has a `review_state`:

| State | Meaning | Required action |
|---|---|---|
| `current` | Re-inspected at its pin within the allowed age | none |
| `review-required` | Upstream changed or the check is older than 120 days | Re-inspect source; update wording, pin, date and consumers; `check_bundle.py` warns on every consuming page |
| `deprecated` | Historical only | Remove citations from guidance (citing it is an error) |
| `unsupported` | Known wrong or out of scope | Remove from guidance; leave a short tombstone in `assumptions.md` if users may still hold the belief |

Changing a date without re-inspecting the source is not a review. When a claim changes, every file in its `consumers` list must be revisited; `check_bundle.py` enforces that consumers cite the claim and that citations are listed, so the list stays accurate.

Drill (exercised by `tests/test_check_bundle.py::Registry::test_synthetic_upstream_change_flags_consumers`): marking `C-TMR-001` review-required after a synthetic upstream timer change makes the check report exactly its three consuming pages.

## Support matrix and versions

- Support levels (`researched`, `feasibility-only`, `compiled`, `host-tested`, `device-verified`, `unsupported`) live in `compatibility.json`; the checker refuses a level whose evidence is not `pass`.
- Initially one official target (1.5.1 / f7 / API 88.2). A previous baseline remains supported only if actively rechecked. `dev`, Momentum and Unleashed are informational until separately qualified.
- Toolkit versions follow SemVer: patch = guidance correction; minor = new tested capability; major = changed skill contracts, installation topology or compatibility policy. Deprecations last at least one minor release where practical; a dangerous or wrong example is removed immediately with a tombstone.

## Licensing and provenance

Original prose, scripts and fixtures are MIT (see `LICENSE`). Upstream firmware is GPL-3.0: link to pinned files, do not copy implementations into MIT material. Record license and provenance for any adapted third-party file in `THIRD_PARTY_NOTICES.md` before merging it. A repository-level MIT label does not relicense mirrored vendor documentation; no documentation mirror is vendored.

## Wizard constants

When the supported baseline changes, update `scripts/fred_setup.py` together with `compatibility.json`: SDK URL and SHA-256, the SDK API-table SHA-256, the toolchain version, and the hash-pinned uFBT requirements. Then run `fred_setup.py sdk` in a scratch folder and `doctor` to confirm.

## Release checklist (local)

1. `python3 scripts/check_bundle.py` → 0 errors.
2. `python3 -m unittest discover -s tests -v` → all pass.
3. `compatibility.json` evidence fields reflect only executed checks; pending gates stay `not-run`/`blocked`.
4. `CHANGELOG.md` lists changes, checks with exact results and pending gates.
5. Activation/pilot/device gates per `evaluations/rubric.json`; a release missing them is labeled **preview** and never "production-ready".
6. Tag only after the above; publishing is a separate explicit action.

## Expansion threshold

Add a skill, protocol reference, adapter, plugin or MCP server only when all hold: at least three distinct real user tasks blocked by the same gap; a benchmark task demonstrating the failure; a named maintainer with a test setup (hardware if physical); and evidence that the added context and maintenance cost is repaid. Track candidates in issue #11.

## Issue templates

Use `.github/ISSUE_TEMPLATE/`: `bug.yml` (toolkit/client/firmware/SDK versions, task, expected/actual, redacted evidence, reproduction, hardware), `stale-source.yml` (claim ID, old/new source, affected pages, correction), `capability-request.yml` (required role/operations, user value, surface, test availability, maintainer).
