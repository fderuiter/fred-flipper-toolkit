# ADR 0002: Guided setup wizard

Date: 2026-10-16. Status: accepted (user request). Amends [ADR 0001](0001-four-skill-bundle.md), which deferred any installer.

## Context

The first release shipped manual copy instructions only. The user asked for setup that humans can follow easily, with wizards for installation, health checks and SDK setup. Manual setup had real pitfalls:
- installing an incomplete set of skills;
- letting uFBT's first run follow the moving release channel;
- `--local` needing `--hw-target`;
- using a non-official toolchain, which changes the generated API.

## Decision

Add `scripts/fred_setup.py`, a standard-library, opt-in wizard for maintainers and users. It offers `install`, `uninstall`, `doctor`, `sdk`, `build` and `card`, plus a guided menu.

- It writes, downloads or runs external programs only after showing the plan and getting confirmation (`--yes` for scripts). `--dry-run` changes nothing.
- It installs all four skills together. Before writing it validates the source with `check_bundle.py`, and afterwards it validates the installed copy. Changed copies are backed up instead of overwritten. It refuses to write through symlinks.
- The SDK setup pins uFBT and its dependencies by hash and verifies the SDK archive's published SHA-256. Everything goes in one isolated folder; no global uFBT state is touched.
- It never touches a Flipper device. It never changes Claude Code settings, hooks or permissions, and it never auto-updates or runs on a schedule.
- Installed skills do not depend on it at runtime.

`check_bundle.py` and `compare_api.py` remain the only offline helpers and keep their stricter rules: no network, no subprocesses. The wizard is a separate tool and is tested in `tests/test_fred_setup.py` without network or device access.

## Consequences

Setup is a single guided command, and `doctor` gives users and maintainers a quick diagnosis. The wizard is more code to maintain: when the baseline changes, its pinned constants (SDK URL, hashes, toolchain version) must be updated together with `compatibility.json`. That step is part of the [maintenance review](../maintenance.md).
