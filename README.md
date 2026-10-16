# FRED: Flipper Resource & Event Driver

An AI development toolkit for the Flipper Zero ecosystem: four Agent Skills for Claude Code plus portable, source-grounded domain references for other coding agents. FRED is a skill/reference bundle; its name does not imply a new embedded runtime or hardware driver.

**Status: v0.1 preview.** The skills, shared references, claim registry, offline helpers, tests and benchmark definitions are implemented and pass their offline checks. The skills pass live activation tests in Claude Code, and the designated reference fixtures compile against the byte-verified official 1.5.1 SDK. **Not yet done:** real-device qualification; nothing has run on a Flipper, so do not treat FRED as production-ready. See [current evidence](#compatibility-and-evidence).

## What it does

| Skill | Use it for |
|---|---|
| `flipper-core` | Feasibility and compatibility: which surface (native FAP, JS, built-in, firmware, host companion, external MCU) can do the job, and which APIs are really exported |
| `flipper-native` | Implementing official-firmware native FAP changes with a proportionate UI architecture, explicit ownership and bounded I/O |
| `flipper-review` | Causal review/debugging of lifecycle, concurrency, callback-context, parser and API-boundary defects |
| `flipper-verify` | Builds, SDK pinning and migration, host/device test planning and release evidence |

All four load the same shared rules in `skills/flipper-core/references/` ([engineering rules](skills/flipper-core/references/engineering-rules.md), [evidence policy](skills/flipper-core/references/evidence-policy.md)) and only the topic references they need. Implementation support targets official-firmware native FAPs; JavaScript, firmware, host and companion-MCU paths get feasibility guidance only.

Examples of what the references get right, each traced to pinned source: `malloc` is the normal exported allocator and crashes on exhaustion instead of returning NULL; raw `printf` is not exported but the wrapped variant is; `furi_timer_stop` does not wait for an in-flight callback; storage calls block; the stock BLE stack is a peripheral, so an appliance that needs a BLE central should be driven through a host or companion MCU.

## Quick start

```sh
git clone https://github.com/fderuiter/fred-flipper-toolkit.git
cd fred-flipper-toolkit
python3 scripts/fred_setup.py          # guided menu: install, check, SDK setup
```

On Windows use `py -3 scripts\fred_setup.py`. The wizard shows its plan and asks before writing anything; `--dry-run` shows the plan without changes. Restart Claude Code afterwards, then ask naturally, for example "Can a stock Flipper control my BLE kettle?", "Add a settings screen to this FAP", "Review app_free() for use-after-free" or "Pin my build to the 1.5.1 SDK".

| Guide | For |
|---|---|
| [Getting started](docs/GETTING-STARTED.md) | Step-by-step install, health check and optional SDK setup |
| [Using FRED](docs/using-fred.md) | What each skill does, example requests, how to read the answers |
| [Troubleshooting](docs/troubleshooting.md) | Common problems and fixes |
| [Manual install and other agents](docs/project-instructions.md) | Copying folders by hand, routing snippet, non-Claude clients |

The skills grant no tool permissions and run no commands on activation. Install all four together; a partial install is reported as broken.

## Repository layout

- `skills/` — the installable payload: four `SKILL.md` entry points, shared [references](skills/flipper-core/references/) and [forms](skills/flipper-core/assets/) (compatibility card, ownership table, validation report, device test plan).
- `skills/flipper-core/references/sources.json` — claim registry: each material claim with pinned source, date, evidence status, consequence and consuming pages.
- [compatibility.json](compatibility.json) — baseline identities and evidence status, kept separate from support claims.
- `scripts/fred_setup.py` — setup wizard (install, doctor, SDK setup, build, compatibility card); see [ADR 0002](docs/adr/0002-setup-wizard.md).
- `scripts/check_bundle.py` — offline bundle/registry/link/budget checker. `scripts/compare_api.py` — offline `api_symbols.csv` comparator.
- `tests/` — unittest suites, including negative and security cases.
- `evaluations/` — seven benchmark tasks, 24 activation cases, rubric and reviewer-side oracles ([overview](evaluations/README.md)).
- `docs/` — [architecture and schemas](docs/architecture.md), [installation](docs/project-instructions.md), [evaluation plan](docs/evaluation.md), [maintenance policy](docs/maintenance.md), [ADR](docs/adr/0001-four-skill-bundle.md), [roadmap](docs/ROADMAP.md), and the [research record](docs/research/README.md).

## Checks

```sh
python3 scripts/check_bundle.py
python3 -m unittest discover -s tests -v
python3 scripts/compare_api.py --old OLD_api_symbols.csv --new NEW_api_symbols.csv
```

Python 3 standard library only; no network, subprocesses or installs. Exit codes are documented in [docs/architecture.md](docs/architecture.md#helper-contracts).

## Compatibility and evidence

Baseline (checked 2026-10-16): official firmware **1.5.1**, commit [`9e810dc`](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065), target **f7**, API **88.2**; uFBT 0.2.6; f7 SDK SHA-256 `dfde39df…2ffe` (downloaded bytes verified); Flipper toolchain 39 (gcc 12.3.1).

| Evidence | Status |
|---|---|
| Documentation reviewed / pinned source inspected | pass (registry re-resolved at its pins) |
| Bundle, registry and helper checks | pass (offline) |
| Fixture compilation against the 1.5.1 SDK | pass — 6 designated apps, 0 warnings, API 88.2 ([record](evaluations/results/2026-10-16-sdk-qualification/README.md)) |
| Host tests of fixture logic | pass — gcc/clang, ASan/UBSan ([record](evaluations/results/2026-10-16-sdk-qualification/README.md)) |
| Live activation (24 cases + 6 held-out) | pass — 23/24, 0 negative false activations, 6/6 held-out ([record](evaluations/results/2026-10-16-activation-summary.md)) |
| Controlled model comparison | see [#8](https://github.com/fderuiter/fred-flipper-toolkit/issues/8) |
| Device behavior | not run ([#9](https://github.com/fderuiter/fred-flipper-toolkit/issues/9)) |

Native FAP support is **host-tested** in FRED's terms (designated fixtures compiled and their logic host-tested), not device-verified. Other firmware lineages are not supported. Compilation will never be presented as proof of hardware behavior.

## Scope

No custom MCP server, installer/updater, app scaffolder, simulator, subagents or protocol micro-skills in v0.1; expansion follows [docs/maintenance.md](docs/maintenance.md#expansion-threshold). Hardware actions (deploy, flash, GPIO output, transmission, HID, tag writes, appliance commands) always need explicit user scope.

## Contributing and license

Read [CONTRIBUTING.md](CONTRIBUTING.md), [AGENTS.md](AGENTS.md) and [SECURITY.md](SECURITY.md). Original repository material is [MIT licensed](LICENSE); linked upstream materials keep their own licenses ([THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)). Roadmap: [tracking issue #1](https://github.com/fderuiter/fred-flipper-toolkit/issues/1).
