# Architecture and data contracts

Implements [ADR 0001](adr/0001-four-skill-bundle.md). This page is the normative definition of the frontmatter grammar, the claim registry schema and `compatibility.json`, which `scripts/check_bundle.py` enforces.

## Layout

```text
skills/                      installed payload (copy all four together)
  flipper-core/SKILL.md
  flipper-core/references/   shared, authoritative rules and claim registry
  flipper-core/assets/       forms: compatibility card, ownership table, validation report, device test plan
  flipper-native/SKILL.md
  flipper-review/SKILL.md
  flipper-verify/SKILL.md
scripts/                     maintainer helpers (not runtime dependencies of skills)
tests/                       unittest suites and synthetic data
evaluations/                 benchmark definitions; oracles never copied into agent workspaces
compatibility.json           toolkit baseline and evidence status
```

Skills hold workflow and decisions; references hold shared rules and evidence; forms are text templates. There is no MCP server, hook, plugin, installer, scaffolder, simulator or subagent. Non-core skills link `../flipper-core/references/engineering-rules.md` and `../flipper-core/references/evidence-policy.md` relative to their own directory and must report a broken installation if either is missing. Bundle Markdown links may not leave `skills/`.

## Restricted SKILL.md frontmatter grammar

This is a deliberately small subset; it is **not** YAML parsing. Anything outside it is rejected rather than interpreted.

```text
frontmatter  = "---" NL 1*field "---" NL
field        = scalar-field / metadata
scalar-field = key ": " value NL            ; key in {name, description, license, compatibility}
metadata     = "metadata:" NL 1*meta-line
meta-line    = "  " subkey ": " value NL     ; exactly two spaces
key, subkey  = [a-z][a-z0-9_-]*
value        = one line, no leading/trailing space
```

Rules:

- Each key at most once; unknown keys rejected; `allowed-tools`, `hooks`, `context`, `agent`, `model` are forbidden in this bundle.
- No blank lines, comments, tabs, flow collections, block scalars, anchors or quoting. A value must not start with any of ``' " [ ] { } & * ! | > % @ ` # , ? -``, contain `": "` or `" #"`, be a YAML keyword (`true`, `no`, `null`, …) or look numeric.
- Dynamic patterns `` !` ``, `${`, `$(`, `$ARGUMENTS`, `{{` are rejected in frontmatter and body.
- `name`: lowercase letters/digits/hyphens, ≤64 characters, equal to the directory name, no `anthropic`/`claude`.
- `description`: ≤1024 characters, no angle brackets. All four descriptions together ≤300 words.
- `compatibility`: ≤500 characters. `metadata` values are strings.

Body requirements: sections `Purpose`, `Activation`, `Inputs and prerequisites`, `Workflow`, `References`, `Outputs and completion`, `Validation`, `Compatibility boundaries`, `Failure modes`, `Handoffs` (as `##` headings). Each SKILL.md ≤150 lines and ≤1,000 words; `engineering-rules.md` ≤1,200 words.

## Claim registry (`skills/flipper-core/references/sources.json`)

```text
{
  "schema_version": 1,
  "checked_on": "YYYY-MM-DD",
  "pins": { "<pin>": { "repo": "owner/name", "ref": "...", "commit": "<40 hex>" } },
  "claims": [ {
    "id": "C-AREA-NNN",
    "claim": "wording",
    "sources": [ { "url": "https://...", "locator": "path/function/section",
                   "pin": "<pin>" | omitted when "live": true } ],
    "applies_to": "version or commit scope",
    "checked_on": "YYYY-MM-DD",
    "status": "documented | source-confirmed | inferred | unresolved",
    "review_state": "current | review-required | deprecated | unsupported",
    "consequence": "practical consequence",
    "consumers": ["flipper-core/references/<file>.md", ...],
    "research_ids": ["S01", ...],          optional link to the research evidence register
    "notes": "..."                          optional
  } ]
}
```

Checks: unique IDs; enum values; ISO dates not in the future; `current` claims checked within `--max-age-days` (default 120) or flagged stale; GitHub URLs pinned to a full commit (no `dev`/`main`/`master` refs) and containing their pin's commit unless `live: true`; every consumer exists in the bundle **and** cites the claim; every `C-…` citation in the bundle exists and lists that file as a consumer; citing a `deprecated`/`unsupported` claim is an error and a `review-required` claim a warning. When a claim changes, its `consumers` list is the set of pages to revisit.

## compatibility.json

`baseline` must contain `firmware`, `target`, `api`, `sdk_artifact`, `ufbt_bootstrap` and `toolchain`, each with `status` (claim status enum) and `checked_on`; unknown values are recorded explicitly, and `unresolved` entries need a `consequence`. `sdk_artifact.published_sha256` is 64 hex; `bytes_verified: true` requires a matching `observed_sha256`.

`evidence` has exactly the fields `documentation_reviewed`, `source_inspected`, `static_checks`, `compiled`, `host_tests`, `device_behavior`, each `{status: pass|fail|not-run|blocked|not-applicable, reason, evidence?}`; `pass`/`fail` need an evidence path or URL that exists.

`surfaces[].support` is one of `researched`, `feasibility-only`, `compiled`, `host-tested`, `device-verified`, `unsupported`. Claiming `compiled` requires `evidence.compiled = pass`; `host-tested` also requires `host_tests = pass`; `device-verified` requires `compiled` and `device_behavior = pass`. This keeps researched targets separate from executed support.

## Helper contracts

- `scripts/check_bundle.py [--root R] [--skills-dir D] [--bundle-only] [--today YYYY-MM-DD] [--max-age-days N] [--format text|json]` — exit 0 clean, 1 errors, 2 usage. `--bundle-only --skills-dir ~/.claude/skills` checks an installed copy. Also validates evaluation definitions and repository Markdown links/anchors in full mode.
- `scripts/compare_api.py --old A.csv --new B.csv [--format text|json] [--fail-on removed,changed,major,minor,added,none]` — exit 0 no selected regressions, 1 regressions, 2 usage/malformed input. Text and JSON output are sorted and deterministic.

Neither helper uses the network, subprocesses, `eval`/`exec`, or imports checked files; neither claims to prove C lifecycle correctness. Tests in `tests/` enforce these properties.

`scripts/fred_setup.py` is the separate, opt-in setup wizard ([ADR 0002](adr/0002-setup-wizard.md)): `install`, `uninstall`, `doctor`, `sdk`, `build`, `card`. It may download and run uFBT, but only after showing the plan and getting confirmation (`--yes` for scripts, `--dry-run` to preview). It validates bundles with `check_bundle.py` before and after installing, backs up replaced copies, pins uFBT dependencies by hash and verifies the SDK archive digest. Exit codes: 0 success, 1 problems found (`doctor`, failed build), 2 stopped (refusal, missing confirmation, hash mismatch, failed command).
