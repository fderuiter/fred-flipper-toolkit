# Research record

Checked 2026-10-16. Start with the [full report](2026-10-16-research-design.md); its first ten sections follow the original research assignment's deliverable order. The [standalone implementation prompt](../implementation-prompt.md) is duplicated in section 10 for a self-contained handoff. When editing the prompt, update both copies together.

[source-inventory.json](source-inventory.json) records repository head pins, observed metadata, the official baseline and published SDK index digest. The report's S01–S30 register records claim-level source/evidence/consequence details. Repository trees, selected firmware/third-party source texts and official docs were read during research. Raw upstream source snapshots are not vendored here; the commit links identify the inspected versions.

## Evidence actually completed

| Category | Status |
|---|---|
| Official docs and selected third-party docs reviewed | Completed within stated scope |
| Selected pinned firmware/build/catalog/skill source inspected | Completed within stated scope |
| Dependency/toolchain installation | Not performed |
| SDK archive bytes verified against published hash | Not performed; index metadata only |
| FAP compilation or application host tests | Not performed |
| Controlled general-agent versus toolkit evaluation | Not performed |
| Hardware/device interaction or behavioral validation | Not performed |

The source set is substantial, not exhaustive. Third-party tests are author claims unless explicitly independently repeated later. Live documentation may drift; commit-pinned source is the implementation reference. Software capability does not establish electrical safety, RF permissions, or physical performance.

The original read-only assignment was later extended by the user to authorize research/design commits directly on this repository's `main` and roadmap issue creation. It did not authorize implementation, dependency installation or hardware activity in this phase.
