# FRED evaluations

Benchmark definitions for the controlled comparison (issue #8). Live activation and SDK qualification records are in `results/`. The full procedure is in [docs/evaluation.md](../docs/evaluation.md).

| Path | Visibility | Content |
|---|---|---|
| `fixtures/<task>/` | Agent-facing: copied alone into a fresh workspace | Synthetic task inputs |
| `tasks/*.md` | Reviewer-side | Task definitions and expected behavior |
| `oracles/*.json` | Reviewer-side | Assertions, seeded defects, expected parses, held-out variants |
| `activation.json` | Reviewer-side | 24 activation cases |
| `rubric.json` | Reviewer-side | Predeclared scoring, critical failures, gates |
| `reference/` | Reviewer-side | Reference solutions and host tests used for SDK qualification; never copied into agent workspaces |
| `harness/` | Reviewer-side | `run_activation.py` (live activation), `run_pilot.py` (paired pilot, `--set training|heldout`), `measure_reference_load.py` (offline per-file token cost from pilot records), `build_fixtures.sh` (uFBT builds), `sandbox.py` (Linux namespace sandbox for `run_pilot.py --sandbox`, with an offline `probe`) |
| `heldout/` | Reviewer-side (its `fixtures/` are agent-facing) | Held-out task variants, oracles and references ([README](heldout/README.md)); never used for tuning |
| `results/` | Reviewer-side | Retained run records |

Isolation rule: an evaluated agent's workspace contains only one `fixtures/<task>/` copy plus normal project facts; the treatment arm additionally gets the installed `skills/` payload. This repository, the research report, issue bodies, task definitions and oracles are never readable by the evaluated agent. A separate folder inside a readable checkout is not isolation. With `run_pilot.py --sandbox`, each session's filesystem holds only its workspace, a fresh HOME and read-only system folders, and its network reaches only the model API; the sandbox proof is in [`results/2026-10-16-sandbox-proof/`](results/2026-10-16-sandbox-proof/README.md).

All fixtures are original synthetic material (MIT). The appliance, sensor protocol and API tables in fixtures are invented for testing and are labeled as such. Fixture code is an evaluation input, not a distributable example.
