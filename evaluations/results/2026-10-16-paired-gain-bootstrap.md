# Paired-gain uncertainty for pilot rounds 1 and 2 (issue #23)

Retrospective analysis of the retained, already graded records. No sessions were run and no scores were changed. Computed with `bootstrap_ci` and `bootstrap_ci_clustered` from `evaluations/harness/run_pilot.py`. Each interval uses 10,000 resamples, seeded with the round's own randomization seed.

| Round | Pairs | Mean paired gain | 95 % CI, pairs resampled | 95 % CI, tasks resampled (sensitivity) |
|---|---|---|---|---|
| 1 (seed 20261016) | 21 | +7.2 | [+1.2, +13.0] | [−1.9, +16.1] |
| 2 (seed 20261017) | 21 | +9.5 | [+4.0, +15.2] | [+1.3, +19.2] |

Per-task paired differences in round 2 (treatment − baseline, three repetitions each):

| Task | Differences |
|---|---|
| 01 utility | +3.0, −4.0, −0.5 |
| 02 settings | +29.0, +30.0, +10.0 |
| 03 UART | +3.0, +22.0, +1.0 |
| 04 `.ir` parser | −1.0, +1.0, −1.0 |
| 05 BLE feasibility | +26.0, +35.0, +30.5 |
| 06 lifecycle review | −0.5, +17.0, 0.0 |
| 07 SDK migration | −3.5, −1.0, +3.0 |

**Reading.** In round 2 the treatment arm's paired gain is positive under both resampling schemes. The gain is driven by three task families (settings, UART, BLE feasibility). The ≥ 10 threshold lies inside both intervals, so these data cannot say whether the gain meets it. Repetitions inside a task are not independent draws, so the task-clustered interval is the more conservative one. Both rounds used the training tasks, and round 2 followed an improvement round shaped on them. The gate decision for #8 therefore waits for the held-out paired run (#12), which `run_pilot.py report` will summarize with the same two intervals.
