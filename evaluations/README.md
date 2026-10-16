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
| `harness/` | Reviewer-side | `run_activation.py` (live activation), `build_fixtures.sh` (uFBT builds) |
| `results/` | Reviewer-side | Retained run records |

Isolation rule: an evaluated agent's workspace contains only one `fixtures/<task>/` copy plus normal project facts; the treatment arm additionally gets the installed `skills/` payload. This repository, the research report, issue bodies, task definitions and oracles are never readable by the evaluated agent. A separate folder inside a readable checkout is not isolation.

All fixtures are original synthetic material (MIT). The appliance, sensor protocol and API tables in fixtures are invented for testing and are labeled as such. Fixture code is an evaluation input, not a distributable example.
