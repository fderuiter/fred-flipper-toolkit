# Evaluation results

No runs have been performed. Each future run is recorded as one directory `results/<date>-<run-id>/` containing:

- `run.json` — exact model name/version, reasoning setting, client and version, tools and permissions, budget (time/tokens), SDK/cache identity, hardware availability, repository commit, skill payload commit, randomization seed and session order;
- `sessions/<task>-<rep>-<arm>/` — transcript, final workspace diff/artifacts, loaded skill/reference files, token counts where exposed;
- `scores.json` — per-assertion scores, N/A redistributions (declared before the run), critical failures, reviewer identity and blinding;
- `activation.json` — per-case loaded skills and pass/fail;
- `summary.md` — means per task and arm, paired differences, gate evaluation, failures and limits.

Rules: keep failed, aborted and blocked sessions; do not re-run a session to replace an unfavorable result without recording both; a pilot of 42 sessions is not statistical proof.
