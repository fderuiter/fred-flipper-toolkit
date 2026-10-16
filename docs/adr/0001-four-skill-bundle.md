# ADR 0001: Four development skills sharing one reference set

Date: 2026-10-16. Status: accepted design; implementation and measured effectiveness pending.

## Context

FRED needs to help agents decide whether a request is feasible on the chosen Flipper surface, implement native changes, detect lifecycle defects and report honest verification. A monolithic prompt eagerly loads unrelated material; protocol micro-skills, agents and device servers create routing and maintenance overhead before their benefit is measured.

## Decision

Ship `flipper-core`, `flipper-native`, `flipper-review` and `flipper-verify` together. Use native SKILL.md metadata and portable Markdown domain content. Keep shared references under core and explicitly read them from every entry point, including direct entry into a non-core skill. Support project and personal Claude Code installation first. Other clients receive portable content, with tested integration listed only after actual checks.

Baseline the first qualification on official 1.5.1/f7/API 88.2. Treat other firmware lineages and JS/host/MCU implementation as separate scopes. Initial helpers check bundle integrity and explicit API-table changes; they do not claim to prove arbitrary lifecycle correctness.

Do not initially add an MCP server, plugin, hooks, installer/updater, scaffolder or comprehensive simulator. Use upstream tools and pinned examples. Future expansion must address repeated tasks and a measured gap.

## Consequences

The suite must be installed intact; missing siblings are an installation failure. Reference paths require tests at both scopes and from unrelated working directories. Agent clients that isolate skill directories need a deliberate adapter. Shared rules avoid drift, while bounded entry points and selective references limit context.

The [research report](../research/2026-10-16-research-design.md#5-architecture-decision) contains alternatives, proposed paths, budgets and validation gates. No installable skill files are introduced by this decision record.
