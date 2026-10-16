# Changelog

## Unreleased

- `flipper-verify` (#21): an SDK or firmware migration now requires a source / binary-import / behavior impact table (evidence and action per row, citing C-FAP-004) and a rebuild plan (target SDK identity, `ufbt` command, import check, pending device cases). Skill description unchanged, so activation was not re-run. The migration-family score is not yet re-measured (#12).

## v0.1.0-preview — 2026-10-16

Release notes: [docs/releases/v0.1.0-preview.md](docs/releases/v0.1.0-preview.md). Handoff for future work: [docs/handoff/next-agent-prompt.md](docs/handoff/next-agent-prompt.md).

Implementation phase started 2026-10-16.

- Compatibility and evidence contracts (#2): `compatibility.json` with separate baseline identities (firmware 1.5.1 / `9e810dc` / f7 / API 88.2, SDK URL and published digest, uFBT 0.2.6, toolchain unknown) and evidence fields; claim registry `skills/flipper-core/references/sources.json` (43 claims with pinned sources, status, consequence and consumers); evidence policy and forms.
- Four skills (#3): `flipper-core`, `flipper-native`, `flipper-review`, `flipper-verify` with restricted frontmatter, direct-entry loading of shared rules and an explicit broken-installation error.
- Shared references (#4): engineering rules, compatibility, capability map with BLE stress test, runtime/UI/ownership, storage/parsers, hardware/communications, build/debug/release, corrected assumptions, pinned upstream example index.
- Offline helpers (#5): `scripts/check_bundle.py` and `scripts/compare_api.py`, with unittest cases including negative and security tests.
- Benchmarks (#6): seven tasks with synthetic fixtures, reviewer-side oracles (eight seeded lifecycle defects, expected parses, held-out variants), 24 activation cases and a predeclared rubric.
- Maintenance (#10): review cadence, claim lifecycle, support matrix, SemVer, licensing, release checklist, expansion threshold and issue templates.
- Baseline facts rechecked at the pins during implementation (API 88.2 row, timer/record/heap/queue/storage/serial/GAP sources). Observed official `dev` `3fb1d51` is API 88.4 with 42 added exports and no removals.

- Live activation (#3): reviewer-side harness; after one description-tuning round 23/24 with 0/8 negative false activations at project and personal scope on claude-sonnet-5-5 and claude-opus-5-5 (Claude Code 2.1.301), held-out paraphrases 6/6.
- SDK qualification (#7): official 1.5.1 f7 SDK bytes verified against the published SHA-256; uFBT 0.2.6 (PyPI digest verified) in an isolated UFBT_HOME; Flipper toolchain 39. Six designated apps build with 0 warnings (APPCHK Target 7, API 88.2) and pass `ufbt lint` after `ufbt format`; reference host tests pass under gcc/clang with ASan/UBSan. Multi-source FAPs are not bit-for-bit reproducible across clean builds (object order).
- New claims C-BUILD-004 (a distro toolchain changed the generated API to 89.0) and C-BUILD-005 (official toolchain identity).

- Setup wizard (`scripts/fred_setup.py`, ADR 0002): guided install/uninstall with backups and verification, `doctor`, hash-pinned SDK setup with archive verification, `build`, and a compatibility-card form; 22 offline tests. New human guides: Getting started, Using FRED, Troubleshooting.
- Pilot (#8): round 1 treatment 84.6 vs baseline 77.4 (−58% critical/major errors); one improvement round of generic guidance (C-GUI-003, storage testing, migration impacts); round 2 treatment 86.7 vs 77.2 (+9.5 paired, −53% critical/major). Comparative gate not met: the migration family stays below 80, there was one treatment critical, and the token ratio is 1.75×. Follow-ups #12, #13, #14, #21.
- Expansion research (#11): fork API surfaces, loader major-only rule (C-FAP-004), mJS host tests (C-JS-001), emulator fidelity, MCP audits, GPIO limits (C-GPIO-001).

Checks: `python3 scripts/check_bundle.py` → 0 errors, 0 warnings; `python3 -m unittest discover -s tests -v` → 96 tests OK (Python 3.13.16, Linux).

Pending: controlled 42-session comparison (#8) and device qualification (#9). No device-tested or production-ready support is claimed.

## Research foundation — 2026-10-16

- Research/design foundation checked 2026-10-16: landscape, correction audit, execution environments, hardware/API boundaries, proposed four-skill architecture, contracts, verification gates and maintenance policy.
- Added a self-contained implementation handoff, pinned repository inventory, and official f7 SDK index URL/digest metadata.
- Corrected README assumptions about fixed heap, universal nonblocking execution and regex-based lifecycle guarantees; retained the FRED name and MIT license.
- Added shared project/contribution/security guidance. Toolkit implementation, SDK compilation, comparative agent evaluation and device qualification remain pending.

- Linked roadmap tracker #1 and work issues #2–#11, with dependency order, acceptance criteria, and separate SDK/model/device qualification gates.
- Refined benchmark isolation so evaluated agents cannot read the research report or expected findings.

