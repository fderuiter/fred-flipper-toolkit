# FRED: Flipper Resource & Event Driver

A proposed AI development toolkit for the Flipper Zero ecosystem, primarily for Claude Code, with portable domain knowledge for other coding agents.

**Status: research and design. There is no installable toolkit yet.** No FAP compilation, model benchmark, or device validation has been completed. FRED is a skill/reference bundle; its name does not imply a new embedded runtime or hardware driver.

## Start here

- [Research and design report](docs/research/2026-10-16-research-design.md): recommendation, landscape, assumption audit, ecosystem boundaries, architecture, four skill contracts, verification gates, roadmap, unresolved decisions, and complete implementation prompt.
- [Standalone implementation prompt](docs/implementation-prompt.md): concrete handoff for the later coding phase.
- [Research evidence and source pins](docs/research/README.md): scope, provenance, known limits, and machine-readable repository inventory.
- [Architecture decision](docs/adr/0001-four-skill-bundle.md): why four cohesive skills and shared references.
- [Roadmap issues](https://github.com/fderuiter/fred-flipper-toolkit/issues): implementation and qualification work with acceptance criteria.

## Intended first release

Four skills, installed together, are proposed:

| Skill | Intended outcome |
|---|---|
| `flipper-core` | Source-grounded research, feasibility, execution-environment selection, and compatibility planning |
| `flipper-native` | Maintainable official-firmware FAP development with appropriate UI, ownership, storage, and peripheral patterns |
| `flipper-review` | Concrete lifecycle, concurrency, parser, and API-boundary review/debugging |
| `flipper-verify` | Reproducible build/test/release evidence and SDK migration checks |

The initial baseline is **official firmware 1.5.1 / target f7 / API 88.2**, source commit [`9e810dc`](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065). This is a **researched target**, not a tested-support claim. uFBT bootstrap and downloaded SDK/toolchain identities must be recorded separately.

## Engineering approach

Agent guidance should reflect the actual API and execution context. Physical SRAM is not a fixed application heap allowance. Worker loops and blocking waits can be appropriate; GUI, timer, and interrupt contexts have different restrictions. ViewDispatcher and SceneManager are architectural choices. Teardown must establish that callbacks and workers have stopped before freeing shared state.

Capability assessment comes before implementation. For example, the researched official baseline configures BLE as a peripheral and does not expose a general BLE central/GATT-client workflow. A suitable host or companion microcontroller may be needed for an appliance controller. See the report for source references and limits.

The first release intentionally excludes a custom MCP server, automatic dependency installer, application scaffolder, comprehensive simulator, and broad community-firmware support. Each expansion needs demonstrated user value and validation.

## Validation and contribution

[Evidence policy and release gates](docs/research/2026-10-16-research-design.md#7-verification-plan-for-the-toolkit) distinguish source inspection, static checks, compilation, host tests, and real-device behavior. Compilation will never be presented as proof of correct hardware behavior.

Read [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md) before changing this repository. The current phase permits research/design documentation and roadmap tracking; toolkit implementation is a later explicitly requested phase. Device deployment, firmware flashing, and physical outputs require their own scope.

Original repository material is [MIT licensed](LICENSE). Linked upstream materials retain their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
