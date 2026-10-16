# Contributing

FRED is a v0.1 preview. Start from [AGENTS.md](AGENTS.md), [docs/architecture.md](docs/architecture.md), the [research record](docs/research/2026-10-16-research-design.md) and the existing issues.

## Technical claims

Add or change a claim in `skills/flipper-core/references/sources.json` with: exact source URL pinned to a full commit (or `live: true` for unversioned docs), path/function/section, applicable version, date checked, documented/source-confirmed/inferred/unresolved status, practical consequence and consuming pages; cite the claim ID in each consuming page. Distinguish upstream documentation, source behavior, third-party claims and engineering recommendations. Explain conflicting evidence rather than silently choosing one source.

## Changes

- Keep skills within budget (≤150 lines and ≤1,000 words each; ≤300 words of descriptions combined; shared rules ≤1,200 words). Domain detail goes in references, shared rules stay in one place, and every skill must work when entered directly.
- Run `python3 scripts/check_bundle.py` and `python3 -m unittest discover -s tests -v`; include exact results in the commit or issue.
- Helper changes need meaningful positive, negative and security tests; helpers stay offline, subprocess-free and must not evaluate inputs.
- Report compiled SDK/toolchain identity separately from host/device behavior. A missing tool or device is a pending gate; it never justifies fabricated results.
- Do not edit evaluation oracles to match a skill's output after seeing results; rubric changes need a new version.

## Licensing and data

Prefer original concise explanations and pinned upstream links. Copying requires an identified license, preserved notices and a row in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Do not add secrets, private captures, real credentials or device identifiers; use synthetic fixtures.

New domains or infrastructure must meet the expansion threshold in [docs/maintenance.md](docs/maintenance.md). Never rewrite other contributors' history.
