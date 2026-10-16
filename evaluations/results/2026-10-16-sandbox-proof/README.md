# Sandbox proof for evaluated sessions (#14), 2026-10-16

No model session was launched for this record. Every check below ran through the same wrapper (`evaluations/harness/sandbox.py`, `Sandbox.command`) that `run_pilot.py run --sandbox` uses for real sessions.

## Environment

- Ubuntu 24.04.5 container, kernel 6.18.44, util-linux 2.39.3 (`unshare`, `setpriv`, `pivot_root`), Python 3.13, gcc 13, clang 18.1.3.
- Claude Code 2.1.301 (`/opt/claude-code/bin/claude`, mounted read-only at `/opt/fred-claude`).
- Host egress goes through an HTTPS proxy; the sandbox proxy chains to it.

## What the sandbox is

New mount, PID and network namespaces. The root filesystem is a fresh tmpfs holding only read-only `/usr` and `/etc`, the Claude Code binary folder, a read-only `/run/fred` (settings and CA files), the session's workspace at `/work`, a fresh `HOME`, private `/tmp`, five `/dev` nodes and the sandbox's own `/proc`. The old root is detached with `pivot_root` + `umount -l`. The session runs with no capabilities and `no_new_privs`: as uid 65534 when the harness runs as root, otherwise as the operator mapped into a user namespace. The network namespace has only loopback; a forwarder relays `127.0.0.1:3128` to a Unix socket where the harness accepts only HTTPS `CONNECT` to the model API host and logs every request. The repository is public, so this also blocks fetching oracles from GitHub. Any mount that overlaps the repository is refused before launch.

## Results

| Record | Command | Result |
|---|---|---|
| [`probe-root.json`](probe-root.json) | `python3 evaluations/harness/sandbox.py probe --out …` (harness as root) | **pass**, 20/20 checks |
| [`probe-user-namespace.json`](probe-user-namespace.json) | `runuser -u nobody -- env HOME=/tmp python3 evaluations/harness/sandbox.py probe --out …` (unprivileged operator) | **pass**, 20/20 checks |
| [`control-no-sandbox.json`](control-no-sandbox.json) | `… probe --control --offline` (same checks, no sandbox; write/remount checks skipped on the host) | checks fail as expected: the repository, oracles and rubric are readable, host processes are visible, direct egress and api.github.com work |
| [`dry-run.json`](dry-run.json), [`dry-run-run.json`](dry-run-run.json) | `python3 evaluations/harness/run_pilot.py run --sandbox --dry-run --out … --jobs 6` | 42/42 jobs rc 0; both arms see only their workspace (`/work`), treatment additionally `.claude/skills/flipper-*`, repository absent, `cc` compiled and ran, Claude Code starts. Each job records the exact session command it would have run |

Checks in each probe (all inside the sandbox): no effective capabilities; the repository path does not exist; `oracles/01-utility.json` and `rubric.json` cannot be read; a `find /` finds no `rubric.json`, `heldout.json`, `oracles`, research report or `run_pilot.py`; `/home` holds only the session HOME and `/root` is absent; fewer than 10 visible processes; old root gone; writes to `/usr`, `/etc`, `/run/fred` and `mount -o remount,rw /usr` fail; the workspace is writable; a direct TCP connection to 1.1.1.1:443 fails; `curl https://api.github.com/repos/fderuiter/fred-flipper-toolkit` through the proxy is refused (logged as denied); `cd /work && gcc … -fsanitize=address,undefined && ./probe_gcc` passes, likewise with clang (without sanitizers: this host lacks clang's sanitizer runtimes); `make` runs; `claude --version` runs; `https://api.anthropic.com/` is reachable through the proxy.

## Limits

- **Not run with a model session.** The paired smoke run in the #14 acceptance criteria needs approved spend. Command, when approved:
  `python3 evaluations/harness/run_pilot.py run --sandbox --tasks 01-utility,04-ir-parser --reps 1 --out /tmp/pilot-sandbox-smoke --seed <new seed> --jobs 4` (4 sessions), then `grade` and `report` as usual.
- **Authentication inside the sandbox is unproven.** The sandbox passes `ANTHROPIC_API_KEY` and `ANTHROPIC_BASE_URL` through; `--sandbox-credentials ~/.claude/.credentials.json` copies an OAuth credentials file into the session HOME, where the evaluated agent can read it. If Claude Code needs another host (for example for token refresh), add it with `--sandbox-allow-host`; denied requests appear in each session record as `proxy_denied`.
- Host `/etc` and `/usr` are readable (read-only). They hold no repository content here; the launch-time overlap check refuses a repository located under them.
- Linux only. As a normal user it needs unprivileged user namespaces, which Ubuntu 24.04 restricts through AppArmor by default; run the harness with `sudo` there. Both modes were tested only in this container.
- The post-run audit of tool inputs (`private_paths`, `outside_workspace_paths`) is kept; it is now a second line of defence, not the isolation.
