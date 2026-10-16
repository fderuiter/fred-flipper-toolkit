# Changelog

## Unreleased — v0.1.0 preview

Implementation phase started 2026-10-16.

- Compatibility and evidence contracts (#2): `compatibility.json` with separate baseline identities (firmware 1.5.1 / `9e810dc` / f7 / API 88.2, SDK URL and published digest, uFBT 0.2.6, toolchain unknown) and evidence fields; claim registry `skills/flipper-core/references/sources.json` (39 claims with pinned sources, status, consequence and consumers); evidence policy and forms.
- Four skills (#3): `flipper-core`, `flipper-native`, `flipper-review`, `flipper-verify` with restricted frontmatter, direct-entry loading of shared rules and an explicit broken-installation error.
- Shared references (#4): engineering rules, compatibility, capability map with BLE stress test, runtime/UI/ownership, storage/parsers, hardware/communications, build/debug/release, corrected assumptions, pinned upstream example index.
- Offline helpers (#5): `scripts/check_bundle.py` and `scripts/compare_api.py`, with 96 unittest cases including negative and security tests.
- Benchmarks (#6): seven tasks with synthetic fixtures, reviewer-side oracles (eight seeded lifecycle defects, expected parses, held-out variants), 24 activation cases and a predeclared rubric.
- Maintenance (#10): review cadence, claim lifecycle, support matrix, SemVer, licensing, release checklist, expansion threshold and issue templates.
- Baseline facts rechecked at the pins during implementation (API 88.2 row, timer/record/heap/queue/storage/serial/GAP sources). Observed official `dev` `3fb1d51` is API 88.4 with 42 added exports and no removals.

Checks: `python3 scripts/check_bundle.py` → 0 errors, 0 warnings; `python3 -m unittest discover -s tests -v` → 96 tests OK (Python 3.13.16, Linux).

Pending: SDK byte verification and fixture compilation (#7, blocked: no prepared toolchain and the SDK host was unreachable from the implementation session); controlled 42-session comparison and live activation (#8); device qualification (#9). No compiled, device-tested or production-ready support is claimed.

## Research foundation — 2026-10-16

- Research/design foundation checked 2026-10-16: landscape, correction audit, execution environments, hardware/API boundaries, proposed four-skill architecture, contracts, verification gates and maintenance policy.
- Added a self-contained implementation handoff, pinned repository inventory, and official f7 SDK index URL/digest metadata.
- Corrected README assumptions about fixed heap, universal nonblocking execution and regex-based lifecycle guarantees; retained the FRED name and MIT license.
- Added shared project/contribution/security guidance. Toolkit implementation, SDK compilation, comparative agent evaluation and device qualification remain pending.

- Linked roadmap tracker #1 and work issues #2–#11, with dependency order, acceptance criteria, and separate SDK/model/device qualification gates.
- Refined benchmark isolation so evaluated agents cannot read the research report or expected findings.

