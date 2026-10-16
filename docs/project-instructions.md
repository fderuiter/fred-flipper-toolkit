# Installing and using FRED

FRED is four Agent Skills directories that must be installed **together as siblings**. Isolated installation of one skill is unsupported and is reported as a broken installation.

Most people should use the wizard: `python3 scripts/fred_setup.py install` (see [Getting started](GETTING-STARTED.md)). This page covers the manual equivalent and other clients.

## Claude Code (primary client)

From a checkout of this repository, copy the four directories into one of the documented skill locations. Review any existing `flipper-*` directories first; do not overwrite them without comparing.

Project scope (shared with collaborators through the project repository):

```sh
mkdir -p /path/to/your-project/.claude/skills
cp -R skills/flipper-core skills/flipper-native skills/flipper-review skills/flipper-verify \
  /path/to/your-project/.claude/skills/
```

Personal scope (all your projects):

```sh
mkdir -p ~/.claude/skills
cp -R skills/flipper-core skills/flipper-native skills/flipper-review skills/flipper-verify ~/.claude/skills/
```

Windows PowerShell (personal scope):

```powershell
New-Item -ItemType Directory -Force "$HOME\.claude\skills" | Out-Null
Copy-Item -Recurse skills\flipper-core, skills\flipper-native, skills\flipper-review, skills\flipper-verify "$HOME\.claude\skills\"
```

Check the installed copy (offline, read-only):

```sh
python3 scripts/check_bundle.py --bundle-only --skills-dir ~/.claude/skills
```

On Windows use `py -3` instead of `python3` if that is your launcher.

The skills declare no `allowed-tools`, hooks or shell interpolation; your normal Claude Code permission settings apply. FRED does not require the root `scripts/` at runtime.

Restart or reload Claude Code if newly added skills do not appear. Live activation of each skill in a specific Claude Code version is part of the controlled evaluation (issue #8) and is not yet recorded.

## Optional routing snippet

Add to your project's `CLAUDE.md` or `AGENTS.md` only if you want explicit routing; keep your existing instructions. The rules themselves stay in the skill references.

```markdown
## Flipper Zero work
Flipper Zero development uses the FRED skills (flipper-core, flipper-native, flipper-review, flipper-verify).
Target: see docs/compatibility-card.md (fill from skills/flipper-core/assets/compatibility-card.md).
Do not deploy, flash, drive GPIO, transmit or write tags without explicit scope.
```

## Other agents

The Markdown references and forms are client-neutral portable content. Agents that load each skill directory in isolation cannot resolve `../flipper-core/...`; they need a deliberate adapter (for example, installing the four directories as siblings in their own skills location). No client other than Claude Code has been tested; do not treat portability as tested support.

## Updating

Replace all four directories together from the same commit, then re-run the bundle check. Mixed versions are unsupported.
