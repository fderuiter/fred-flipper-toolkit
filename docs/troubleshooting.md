# Troubleshooting

Start with `python3 scripts/fred_setup.py doctor`. It finds most problems and prints the fix.

## Installing and loading the skills

| Symptom | Cause and fix |
|---|---|
| Claude doesn't seem to use FRED | Restart Claude Code or start a new session after installing. Check the install: `doctor`. Ask about your Flipper task directly; FRED does not load for unrelated questions. |
| "Broken installation: FRED shared references are missing" | Only some of the four folders were copied, or a file is missing. Reinstall all four: `python3 scripts/fred_setup.py install`. |
| `doctor` reports "mixed versions" | Folders from different FRED versions are installed together. Reinstall from one checkout; the wizard replaces all four. |
| `doctor` says your copy "differs from this checkout" | Your installed copy is older or locally edited. Run `install` again; your edits are backed up in `.claude/fred-backups/`. |
| "needs confirmation … re-run with --yes" | The wizard is running without a terminal (in a script or CI). Add `--yes` once you have checked the plan with `--dry-run`. |
| Installed in both project and personal folders | Claude Code can see both copies, so keep them the same version (`doctor` shows each) or remove one with `uninstall --scope personal` or `uninstall --scope project`. |
| Skills are missing under `claude --restricted` | Restricted mode did not load project-scope skills in testing (Claude Code 2.1.301). Use a normal session. |
| `.claude/fred-backups/` appears in `git status` | These are the wizard's safety copies. Delete them when you no longer need them, or add `.claude/fred-backups/` to your `.gitignore`. |

## SDK and builds

| Symptom | Cause and fix |
|---|---|
| `sdk` stops with "SHA-256 mismatch" | The download is corrupt or not the official file. Nothing was used. Delete `~/.fred/sdk-1.5.1-f7/downloads/` and try again. If it repeats, do not proceed. |
| Download fails or times out | The network or a proxy blocks `update.flipperzero.one` or `pypi.org`. Allow those hosts, or download the SDK elsewhere and pass `--sdk-zip PATH`; it is still hash-checked. |
| `ufbt update --local=…` says "HW target must be specified" | Add `--hw-target=f7`. The wizard does this for you. |
| First build downloads ~350 MB | That is the official toolchain (version 39). It happens once per SDK folder. |
| A build somewhere else shows API 89.0 or other odd versions | That build used a non-official toolchain, and the generated API can change (claim `C-BUILD-004`). Use the wizard's SDK, or `source ~/.fred/sdk-1.5.1-f7/activate.sh`. |
| `ufbt lint` fails | Lint checks formatting only. `ufbt format` rewrites the files; review the diff. A lint pass says nothing about correctness. |
| `ufbt test` not found | There is no such command. Host-test pure logic with your compiler; official firmware unit tests run on the device. |
| Two builds give different `.fap` hashes | Output depends on the build folder and, for multi-file apps, object order. Record the hash of the exact file you deploy. |

## On the Flipper

| Symptom | Cause and fix |
|---|---|
| The Flipper refuses to start the app with an API-version error | The app was built for a different API major; the loader reports it as too old or too new (claim `C-FAP-004`). Rebuild with the SDK matching your firmware. |
| App fails to load with a missing symbol | The app imports something your firmware does not export: a newer minor, a fork-only API, or a private function. Check with `scripts/compare_api.py`. |
| An app from Momentum or Unleashed won't run on official firmware | They are separate lineages with different APIs (claim `C-FORK-001`). Rebuild against the official SDK and replace fork-only calls. |
| JavaScript errors such as "[n] is not defined", "try is not implemented", "Use ===" | Flipper's JS engine has no closures over outer variables, no `try`/`catch`, no JSON, and rejects `==` and implicit conversions (claim `C-JS-001`). Write plain ES5-style code. |
| UART app shows "USART busy" | Another owner (CLI/log or an expansion module) holds the port. Close it, or use a different port. |

## Still stuck

Open an issue using the **bug** template. Include the output of `doctor`, your firmware version and what you asked. Do not include secrets, captures or device identifiers.
