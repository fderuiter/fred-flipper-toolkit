# Compatibility

Use with the [compatibility card](../assets/compatibility-card.md). The repository's `compatibility.json` is the machine-readable record of the toolkit's own baseline and evidence.

## Researched baseline (checked 2026-10-16)

| Component | Value | Evidence |
|---|---|---|
| Firmware lineage / tag | official 1.5.1 | [C-BASE-001] source-confirmed |
| Firmware commit | `9e810dc399e027e48ce07751df72168abbb19065` | [C-BASE-001] |
| Hardware target | f7 | [C-BASE-002] |
| Exported SDK API | 88.2 (`Version,+,88.2` row of `targets/f7/api_symbols.csv`) | [C-BASE-002] source-confirmed |
| f7 SDK artifact | `https://update.flipperzero.one/builds/firmware/1.5.1/flipper-z-f7-sdk-1.5.1.zip` | [C-BASE-003] documented (index metadata) |
| Published SDK SHA-256 | `dfde39df32ba8a9789b26afc1d702579ad8399a1c285770637cfe3287b2e2ffe` | [C-BASE-003]; downloaded bytes **not verified** |
| uFBT bootstrap | 0.2.6, release commit `093ab78058f19b11eab82020989aa4e123521490` | [C-BASE-004] documented |
| Toolchain (compiler/binutils) | unknown | Supplied by SDK setup; record when actually installed |
| Application catalog | `5309fa2b142cb3bcc78098ddb1d1e3c7908fce47` | [C-CAT-001] |

This is a **researched target**, not compiled or device-tested support. Do not replace it with a moving release channel; propose a baseline change as maintenance. Pin bootstrap, SDK, toolchain, firmware lineage and dependencies separately: the SDK carries much of the build machinery, so pinning uFBT alone is insufficient [C-BASE-004].

The official `dev` branch observed at `3fb1d51727733d2007abc5ec636a89328857803e` declares API 88.4 and exports 42 symbols absent from 1.5.1 (no removals or signature changes) [C-BASE-005]. A FAP built against dev that imports one of them can fail to load on 1.5.1.

## Three kinds of compatibility

1. **Source compatibility** — the project rebuilds with the chosen headers, signatures and toolchain.
2. **Binary/import compatibility** — target and API major match the firmware's policy *and* every imported symbol resolves at load time, including symbols added in later minors and library/resource dependencies [C-FAP-001] [C-BASE-002].
3. **Behavior** — the app does the right thing on the device. Only device evidence establishes this.

A matching API major or a successful compile proves neither of the others, and never proves cross-fork compatibility [C-FORK-001].

## Compatibility intake

Fill the card from the user's repository first (manifest, SDK state, README, CI config); ask only for unknowns that block a decision. Record:

- firmware lineage, tag, commit; hardware target/board revision;
- execution surface (external FAP, built-in app, firmware service, JS, host companion, expansion MCU);
- SDK API and artifact URL/hash; bootstrap and toolchain versions; build mode;
- imported interfaces and bundled/private libraries (`fap_libs`);
- host OS; available hardware; authorized actions.

Unknown values are legal. Each needs a consequence, for example "SDK unknown → cannot claim imports resolve; build pending".

## Support levels

| Level | Means | Requires |
|---|---|---|
| researched | Guidance traced to pinned source/docs | Claim records |
| feasibility-only | Decision guidance; no implementation support claimed | Claim records |
| compiled | Designated fixtures built against the recorded SDK | Compiled = pass |
| host-tested | Extracted logic tested on host | Compiled and host tests = pass |
| device-verified | Observed on named hardware | Compiled and device = pass |
| unsupported | Known not to work on this surface/baseline | Cited evidence |

## Other lineages

Momentum and Unleashed showed recent activity; Xtreme is archived [C-FORK-001]. Treat each as its own lineage: inspect its `api_symbols.csv`, extensions and toolchain, rebuild and verify imports. No fork support is claimed initially. Compare API tables with the repository's `scripts/compare_api.py` when both tables are available locally.
