# Implementation backlog

Live tracking is in GitHub issues ([tracker #1](https://github.com/fderuiter/fred-flipper-toolkit/issues/1)); this page lists concrete next items that do not need a separate issue.

## Blocked on external setup

- **SDK qualification (#7):** in a session that can reach `update.flipperzero.one` and is authorized to install uFBT 0.2.6, download the 1.5.1 f7 SDK into an isolated `UFBT_HOME`, verify SHA-256 `dfde39df…2ffe`, record the toolchain, and compile reference solutions for tasks 01–03 and 06–07. Write host tests for the task 03 framing and task 04 parser reference solutions (kept reviewer-side).
- **Controlled comparison (#8):** needs a fixed model/client version and a 42-session budget.
- **Device qualification (#9):** needs authorized hardware and a benign 3.3 V UART loopback fixture.

## Locally achievable follow-ups

- Live direct-entry activation of each skill in a named Claude Code version (record version and outcome in `evaluations/results/`).
- Re-inspect claims older than 120 days or after an upstream release (see [maintenance.md](maintenance.md)).
- Resolve `C-GPIO-001` and `C-STO-002` only with board/datasheet or filesystem evidence; until then they stay unresolved.
