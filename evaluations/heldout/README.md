# Held-out benchmark set (issue #12)

Reviewer-side. Seven variants of the training tasks, one per task family, as described in [`../oracles/heldout.json`](../oracles/heldout.json). They were written after the training-round results and **after** the skill changes for #21, #22 and #13, and must never be used to tune skill text. Run them only to measure.

| Held-out task | Variant of | What changes |
|---|---|---|
| `h01-stopwatch` | 01 utility | Lap stopwatch: tick-based timing, bounded lap ring, timer-before-ViewPort teardown |
| `h02-pomodoro` | 02 settings | Settings v1 → v3 where v2 renamed `Break` to `ShortBreak` (rename trap), future v9 file |
| `h03-i2c` | 03 UART | I²C sensor polled from a worker, CRC-8, bounded stuck-bus recovery, injected push-pull/5 V instruction |
| `h04-sub-parser` | 04 `.ir` parser | Bounded Sub-GHz RAW subset parser, 16-file corpus, injected replay instruction |
| `h05-wifi-feasibility` | 05 BLE feasibility | Wi-Fi HTTPS thermostat requested as a FAP-only controller (no Wi-Fi radio) |
| `h06-dispatcher-review` | 06 lifecycle review | ViewDispatcher app with six seeded defects and eight decoys, three user symptoms |
| `h07-variable-migration` | 07 SDK migration | Minor bump (88.2 → 88.4) hiding a Variable type change and a header removal |

Layout mirrors the training set: `fixtures/<task>/` is agent-facing (copied alone into a fresh workspace), `tasks/*.md` and `oracles/*.json` are reviewer-side, `reference/` holds reviewer-side solutions. `check_bundle.py` checks that every task has an oracle and a fixture folder, that oracle weights match `../rubric.json`, and that fixtures carry no oracle markers.

## Reference solutions

| Task | Reference | Host tests | SDK build |
|---|---|---|---|
| h01 | `fred_stopwatch.c` app + `stopwatch_core.c` | 34 checks | pending |
| h02 | `pomodoro_settings.c` (parse, migrate, save/recover) | 48 checks over `samples/`, crash injected at every save step | not applicable (core only) |
| h03 | `hx40.c` (CRC-8, conversion, recovery policy) | 19 checks replaying `captures/` | not applicable (core only) |
| h04 | `sub_parser.c` | 19 checks over `corpus/` | not applicable (core only) |
| h05 | none (feasibility answer) | — | — |
| h06 | `door_monitor.c` corrected, each change labelled `H06-Dn` | — | pending (fixed and flawed apps) |
| h07 | `logger.c` migrated source | — | not buildable (synthetic API tables) |

```sh
sh evaluations/heldout/reference/run_host_tests.sh      # gcc+ASan/UBSan, gcc -O2, clang -O0/-O2
```

The FAP builds (h01 app, h06 fixed and flawed apps) are listed in `../harness/build_fixtures.sh`. They need the pinned 1.5.1 SDK and toolchain 39. The session that wrote this set could not reach `update.flipperzero.one` because the network policy blocked it, so those builds are **pending**, not passed. Two details in the references are inferred rather than registered claims. First, the Sub-GHz band limits in `sub_parser.c` (300–348, 387–464 and 779–928 MHz). Second, how h03 recovery returns the pins to I²C mode, which is left to the FAP wrapper. The oracles accept a stated rule for both.

## Running the paired held-out pilot

This spends model sessions (about $10 for the 42 sessions plus about $10 for grading, per round, at the round-2 rates). It needs the user's explicit approval.

```sh
python3 evaluations/harness/run_pilot.py run    --set heldout --out /tmp/pilot-heldout --seed <new seed> --jobs 6
python3 evaluations/harness/run_pilot.py grade  --set heldout --out /tmp/pilot-heldout --jobs 6
python3 evaluations/harness/run_pilot.py report --set heldout --out /tmp/pilot-heldout
```

Use the same model, effort, tools, prompt and grader as rounds 1 and 2. Copy the graded output into `evaluations/results/` and report it against the same gates whatever the outcome. The report includes a 95 % bootstrap interval over task-repetition pairs and a task-clustered interval as a sensitivity check.
