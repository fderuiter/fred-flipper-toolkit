# Evidence policy

Every FRED skill reports what is known, how it is known, and what remains open. Read with [engineering-rules.md](engineering-rules.md).

## Claim classification

| Status | Meaning | Allowed wording |
|---|---|---|
| `documented` | Official documentation states it; implementation not inspected | "The docs state…" |
| `source-confirmed` | Pinned source/export table inspected at a named path/function | "At 9e810dc, `path:function` …" |
| `inferred` | Engineering conclusion from documented/source evidence | "This implies…; not executed" |
| `unresolved` | Evidence missing or conflicting | "Unknown; blocks …" |

A material claim records: claim ID and wording, source URL and path/function/section, version or commit, date checked, status, practical consequence, and the references/examples that consume it. The registry is [sources.json](sources.json); its schema is in the repository's `docs/architecture.md`. Live (unpinned) sources must be flagged `live` and never presented as commit-pinned.

Review states: `current` (usable), `review-required` (upstream changed or check is old — warn and re-inspect before relying on it), `deprecated` (kept for history, do not cite as guidance), `unsupported` (known wrong or out of scope — remove from guidance and leave a tombstone). Refreshing a date without re-inspection is not a review.

## Execution evidence fields

Report each field separately; never collapse them into one "verified" badge.

| Field | Requires | Does not prove |
|---|---|---|
| Documentation reviewed | URL/section, date, version | Implementation matches |
| Source inspected | Commit, path/function, call path/export/library | Compiles or runs |
| Static checks | Tool + version/config, command, exit code, findings | Behavior under real timing |
| Compiled | App commit, SDK URL/hash/API, toolchain, command, output, artifact hash | UI, teardown, radio, electrical behavior |
| Host tests | Harness, platform, cases/fixtures, results | Firmware scheduler, DMA, buses |
| Device behavior | Hardware/firmware/app identity, steps, observations, repetitions | Other devices, lineages, untested cases |

Each field is `pass`, `fail`, `not-run`, `blocked` or `not-applicable`, with a reason and evidence path. Record it with [validation-report.md](../assets/validation-report.md).

## Rules

1. Never claim an unexecuted check. "Should compile" is not "compiled".
2. Never upgrade compilation, mocks, lint or an emulator into a device pass [C-TEST-001] [C-EMU-001].
3. `ufbt lint` is formatting/naming evidence only [C-BUILD-001].
4. Third-party README or test claims are author claims until repeated.
5. A failed or blocked check stays visible in the report; do not delete it to make a summary look clean.
6. A review hypothesis is not a reproduced crash; label the evidence level of each finding.
7. When sources conflict (docs vs implementation), cite both and say which governs behavior.
8. Researched support, compiled support and device-tested support are different support levels; see [compatibility.md](compatibility.md).
