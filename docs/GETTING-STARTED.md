# Getting started with FRED

FRED gives Claude Code four Flipper Zero skills: feasibility checks, native app development, code review and build/release evidence. This guide takes you from nothing to a working setup. Every step says what it changes on your computer.

| You want to… | Do | Time |
|---|---|---|
| Use FRED in Claude Code | [Step 1](#step-1-get-fred) and [Step 2](#step-2-install-the-skills) | ~2 minutes |
| Also compile Flipper apps | add [Step 4](#step-4-optional-set-up-the-official-sdk-to-build-apps) | ~10 minutes, ~400 MB download |
| Check everything is right | [Step 3](#step-3-check-your-setup) at any time | seconds |

**What FRED is not:** it does not install anything into your Flipper, flash firmware, drive pins or transmit. It helps Claude give correct, evidence-based answers; you stay in control of every device action.

## What you need

- **Python 3.8 or newer.** Check with `python3 --version`. On Windows use `py -3 --version`.
- **Claude Code** for the skills. Other agents can read the same Markdown, but only Claude Code has been tested ([details](project-instructions.md#other-agents)).
- **git**, or a downloaded ZIP of this repository.

Commands below use `python3`. On Windows, type `py -3` instead.

## Step 1: get FRED

```sh
git clone https://github.com/fderuiter/fred-flipper-toolkit.git
cd fred-flipper-toolkit
```

Changes: creates the `fred-flipper-toolkit` folder.

## Step 2: install the skills

Run the setup wizard and follow the prompts:

```sh
python3 scripts/fred_setup.py
```

Choose **Install FRED**, then where to put it:

- **This project only.** Installs into `<your project>/.claude/skills/`. Collaborators get it too if you commit that folder.
- **Personal.** Installs into `~/.claude/skills/` and works in all your projects.

The wizard first checks that the bundle is complete, then shows exactly what it will add or update and asks before writing anything. If an older or edited copy exists, it is backed up to `.claude/fred-backups/` before being replaced. Afterwards it verifies the installed copy.

For project installs it also offers to add a four-line routing note to your `CLAUDE.md`. That step is optional, your existing text is kept, and it is never added twice.

Non-interactive version (for scripts):

```sh
python3 scripts/fred_setup.py install --scope project --project /path/to/your/project --yes
python3 scripts/fred_setup.py install --scope personal --yes
```

Add `--dry-run` to any command to see what it would do without changing anything.

**Then restart Claude Code** (or start a new session) so it picks up the new skills.

Prefer to copy folders by hand? See the [manual install](project-instructions.md#claude-code-primary-client).

## Step 3: check your setup

```sh
python3 scripts/fred_setup.py doctor
```

`doctor` changes nothing. It checks:

- Python version;
- whether FRED is installed in the current project and/or personally, and whether the copies are complete, one version, and in sync with this checkout;
- whether Claude Code is on your PATH;
- if you set up the SDK, that its API table is the pinned 1.5.1 one and the toolchain version matches.

Every problem comes with a suggested command. The exit code is 0 when everything is fine.

## Step 4 (optional): set up the official SDK to build apps

You only need this to compile Flipper apps (`.fap` files). The wizard keeps everything in one folder (default `~/.fred/sdk-1.5.1-f7`) so it never touches another uFBT setup:

```sh
python3 scripts/fred_setup.py sdk --dry-run   # see the exact plan first
python3 scripts/fred_setup.py sdk
```

It asks before each step:

1. **Install uFBT 0.2.6** into a private Python environment. Every package (ufbt, oslex, mslex) is pinned by SHA-256 hash.
2. **Download the official firmware 1.5.1 f7 SDK** (~20 MB) and verify its SHA-256 against the published value. On a mismatch it stops and uses nothing.
3. **Point uFBT at that verified SDK** in an isolated `UFBT_HOME`.
4. *(Optional)* **Build a small test app.** The first build downloads the official toolchain (version 39, ~350 MB) and runs the SDK's build scripts.

Then build any app folder (one containing `application.fam`):

```sh
python3 scripts/fred_setup.py build path/to/my_app
```

The wizard shows the app's `application.fam` before building, because the build runs it as Python. It then prints each `.fap` file's SHA-256 hash.

To use `ufbt` directly, load the environment first. POSIX shells: `source ~/.fred/sdk-1.5.1-f7/activate.sh`. PowerShell: `. ~/.fred/sdk-1.5.1-f7/activate.ps1`.

A successful build proves the app compiles against SDK 1.5.1. It does **not** prove it works on a Flipper. `ufbt launch` would install and start the app on a connected device; run it only when you mean to.

To undo everything, delete `~/.fred/sdk-1.5.1-f7`.

## Step 5: use it

Open Claude Code in your project and ask normally:

- "Can a stock Flipper control my Bluetooth kettle?"
- "Add a settings screen with saved values to this FAP."
- "Review app_free() in this app for use-after-free."
- "Pin my build to the 1.5.1 SDK and tell me what the build proves."

Claude picks the right FRED skill by itself. [Using FRED](using-fred.md) explains each skill, what answers to expect, and how to read the evidence reports.

Optional: describe your project once so every answer starts from the right target:

```sh
python3 scripts/fred_setup.py card --project /path/to/your/project
```

This writes `docs/compatibility-card.md` in your project. It asks before overwriting an existing card.

## Updating and removing

- **Update:** `git pull`, then run `python3 scripts/fred_setup.py install` again. Unchanged skills are skipped, changed ones are backed up and replaced.
- **Remove:** `python3 scripts/fred_setup.py uninstall --scope project` (or `--scope personal`). Only the four FRED folders are removed, and a backup is kept.

Stuck? See [Troubleshooting](troubleshooting.md).
