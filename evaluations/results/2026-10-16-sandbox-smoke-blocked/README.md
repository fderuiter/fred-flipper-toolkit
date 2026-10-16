# Sandboxed paired smoke run (#14), 2026-10-16: blocked on authentication

Spend approved by the user for this run. Actual model spend: $0.

## Command

```sh
python3 evaluations/harness/run_pilot.py run --sandbox \
  --sandbox-credentials /root/.claude/.credentials.json \
  --tasks 01-utility,04-ir-parser --reps 1 --seed 20261017 --jobs 4 \
  --out /tmp/claude-0/smoke/out --work /tmp/claude-0/smoke/work
```

Run at `main` `5cc7722`, Claude Code 2.1.301, inside a claude.ai cloud container running as root.

## Result

| Session | Task | Arm | rc | Result |
|---|---|---|---|---|
| `41c1bce86a` | 01-utility | baseline | 1 | `Not logged in · Please run /login`, 0 tokens, $0 |
| `6e263f94b3` | 01-utility | treatment | 1 | same |
| `bba97a821e` | 04-ir-parser | baseline | 1 | same |
| `671a82f79f` | 04-ir-parser | treatment | 1 | same |

The pre-run sandbox probe passed, so isolation held. Every session stopped before its first model call: Claude Code inside the sandbox had no login. In this container the `.credentials.json` file holds no Claude login. Claude Code's login here is managed by the hosting environment and is not available inside the sandbox. The proxy denied no requests, so no blocked host is involved.

Host tests did not execute in either arm. The acceptance item "a small paired smoke run shows host tests executing in both arms" is **not met**.

The session records stay in the operator's scratch folder and are not committed. They contain no workspace changes, and the sandbox HOMEs held a copy of the credentials file, which was deleted after the run.

## What unblocks it

Run the same command on a Linux machine where Claude Code logs in with a portable credential:

- an `ANTHROPIC_API_KEY` in the environment (passed into the sandbox), or
- a Claude login stored in `~/.claude/.credentials.json`, passed with `--sandbox-credentials` (the evaluated agent can read the copy).

On Ubuntu 24.04, run it with `sudo` (the sandbox needs user namespaces, which AppArmor restricts for normal users).
