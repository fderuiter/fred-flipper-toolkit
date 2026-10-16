# Compatibility card

Copy into the user's project notes (for example `docs/compatibility-card.md`) and fill from the repository before asking questions. `unknown` is allowed; give its consequence. Rules: [compatibility.md](../references/compatibility.md).

| Field | Value | Source / status | Consequence if unknown |
|---|---|---|---|
| User outcome | | | |
| Firmware lineage / tag / commit | | | |
| Hardware target / board revision | | | |
| Execution surface | FAP / built-in / firmware / JS / host / MCU | | |
| SDK API version | | | |
| SDK artifact URL + SHA-256 (verified?) | | | |
| uFBT bootstrap / toolchain versions | | | |
| Imported interfaces / `fap_libs` | | | |
| Host OS | | | |
| Hardware available | | | |
| Authorized actions | read / build / deploy / flash / physical output | | |

## Capability decision

| Required operation | Header | Implementation | Export / library | Owner | Status |
|---|---|---|---|---|---|
| | | | | | documented / source-confirmed / inferred / unresolved |

Chosen surface and reason:

Rejected surfaces and reason:

Material unknowns and what they block:

Validation plan (link [validation-report.md](validation-report.md)):
