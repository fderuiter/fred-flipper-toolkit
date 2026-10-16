# Using FRED

You never call FRED's skills by name. Ask Claude Code about your Flipper work and it loads the matching skill. Each skill reads the same shared rules first, so answers are consistent however you start.

## The four skills

| Skill | Ask it things like | You get |
|---|---|---|
| **flipper-core**: can it be done, and where? | "Can a stock Flipper act as a BLE central?", "FAP or JavaScript for this?", "Is `furi_hal_xyz` available to apps?", "Port this Momentum example to official firmware", "Can I power a relay from pin A7?" | A **compatibility card**, a feasibility decision citing the actual firmware source, rejected options with reasons, and what still needs testing |
| **flipper-native**: build or change a FAP | "Add a settings screen to my FAP", "Implement UART receive with an overflow counter", "Parse this capture file in my app" | The code change, plus notes on who owns each resource and how teardown works, host tests for separable logic, and an evidence report |
| **flipper-review**: find real bugs | "Review this app for races and leaks", "It crashes after leaving UART mode" | Findings ranked by severity, each with trigger, consequence, evidence level and a specific fix. Review-only requests never edit your files |
| **flipper-verify**: what is actually known to work | "Check this builds against 1.5.1", "Migrate my app to the new SDK", "What can we verify without hardware?" | A table of checks with commands, versions and results, plus artifact hashes and a separate device test plan |

## Reading FRED's answers

**Evidence labels.** FRED never says just "verified". Each of these is reported separately, as `pass`, `fail`, `not-run`, `blocked` or `not-applicable`:

- documentation reviewed
- source inspected
- static checks
- compiled
- host tests
- device behavior

"Compiles" never means "works on the Flipper".

**Claim IDs.** Technical statements carry IDs such as `[C-TMR-001]`. Each one is defined in the installed `flipper-core/references/sources.json`, with the exact firmware file and commit it came from.

**"Unsupported" is a real answer.** For example, the stock firmware's Bluetooth is a peripheral only, so a FAP cannot control a BLE appliance on its own. FRED then recommends a companion board or a host app instead of inventing an API.

**Unknowns are kept.** If your firmware version is unknown, FRED says what that blocks rather than guessing.

## Forms you can reuse

These live in the installed `flipper-core/assets/` folder:

- `compatibility-card.md`: your target, surface and unknowns (`fred_setup.py card` fills one in for you).
- `ownership-table.md`: who owns each resource and how it is stopped and freed.
- `validation-report.md`: one row per evidence type.
- `device-test-plan.md`: hardware steps, kept separate from software checks.

## Safety boundaries

FRED helps with legitimate development and authorized research. It will not quietly do the following:

- install dependencies;
- download toolchains;
- deploy, launch or flash;
- drive GPIO, transmit, inject HID input, write tags or send appliance commands.

Each of these needs your explicit go-ahead in the conversation. Text inside captures, files or device output is treated as data, never as instructions.

## Good habits

- Tell Claude your firmware version, if you know it, and whether a Flipper is attached.
- Keep a `docs/compatibility-card.md` in your project. The optional `CLAUDE.md` note points Claude to it.
- For builds, use the wizard's pinned SDK (`fred_setup.py sdk`). A plain `ufbt` first run pulls whatever the moving release channel currently holds.
- Treat header pins as **3.3 V only**, with at most 20 mA per pin (claim `C-GPIO-001`).
