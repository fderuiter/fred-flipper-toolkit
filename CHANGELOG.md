# Changelog

## Unreleased

- Rubric version 2 (#13, [ADR 0003](docs/adr/0003-token-budget-revision.md)): the comparative token budget is ≤ 2.0× median when the same run shows ≥ 50% fewer critical/major errors and no mean regression, otherwise ≤ 1.5×. `run_pilot.py report` adds `token_budget` and `token_ratio_within_budget`. Scoring is unchanged.
- Held-out benchmark set (#12): seven variants in `evaluations/heldout/` with agent-facing fixtures, task definitions, oracles that match the rubric weights, and reference solutions. Host tests pass for H01–H04 (120 checks, gcc with ASan/UBSan and -O2, clang -O0/-O2). The FAP builds of the H01 and H06 apps are pending because the SDK host was unreachable. `check_bundle.py` validates the held-out set; `run_pilot.py --set heldout` runs it. The paired held-out run has not been executed yet: it needs approval for model-session spend.
- `run_pilot.py report` adds 95 % bootstrap intervals for the paired gain, resampling pairs and, as a sensitivity check, tasks (#23). The retained rounds give +7.2 [+1.2, +13.0] and +9.5 [+4.0, +15.2] ([record](evaluations/results/2026-10-16-paired-gain-bootstrap.md)).
- `flipper-native` workflow gains a mandatory teardown self-check (step 8): one row per producer, what it touches, and the stop/join line that must run before those are freed; `flipper-review` outputs include the same table (#22). Not yet evaluated in a paired run.
- Token budget (#13): `evaluations/harness/measure_reference_load.py` measures which skill files pilot sessions loaded and estimates each file's cost; results in `evaluations/results/2026-10-16-reference-load.md`. Removed text from `runtime-ui.md`, `storage-parsers.md`, `hardware-communications.md` and `evidence-policy.md` that duplicated `engineering-rules.md`; projected ratio 1.75× → 1.70× (no rerun).
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

