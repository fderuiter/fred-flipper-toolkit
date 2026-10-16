# Contributing

FRED is in research/design. Start from the [report](docs/research/2026-10-16-research-design.md), [architecture decision](docs/adr/0001-four-skill-bundle.md), and existing issues. Implementation and qualification remain open work.

For a technical claim or correction, include its exact source URL/path/section, firmware/SDK lineage and version or commit, date checked, evidence classification, affected guidance, and practical consequence. Distinguish upstream documentation, source behavior, third-party claims and engineering recommendations. Explain conflicting evidence rather than silently choosing one source.

For later code changes, provide the smallest useful behavior/failure tests and exact checks performed. Report compiled SDK/toolchain identity separately from host/device behavior. A missing tool/device is a pending gate; it does not justify fabricated results.

Prefer original concise explanations and pinned upstream links. Copying code/text requires an identified license and preserved notices. Do not assume the repository's MIT license covers linked or mirrored upstream material. Do not add secrets, private captures or real access credentials.

Keep skills focused and shared rules in one place. Every skill must work when entered directly. New domains or infrastructure should address repeated real tasks and a benchmark gap, with an accountable maintainer and test setup.

Research commits directly on `main` are authorized for this initial session. Subsequent work follows the user's current instructions and repository protections; do not infer perpetual permission for unrelated external actions. Never rewrite other contributors' history.
