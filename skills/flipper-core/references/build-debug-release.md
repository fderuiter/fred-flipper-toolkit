# Build, debug and release

Load when building, migrating SDKs, debugging crashes or preparing distribution.

## Before running anything

- `application.fam` is Python syntax evaluated by the build; FAM fields and `fap_extbuild`-style hooks can run code. Read the manifest and any build hooks before building, even locally. A helper must never `eval`/`exec` a manifest to "check" it [C-FAP-002].
- Generic `App()` requires `appid` and `apptype`; external apps also need a real `entry_point` with the right signature. Catalog submission adds its own manifest/screenshot/license rules [C-FAP-002] [C-CAT-001].
- Running uFBT for the first time downloads SDK and toolchain components — by default from the moving `release` channel [C-BASE-004]. That is a new external effect: it needs the user's authority, a pinned source and an isolated state directory.

## Verified commands (meaning, not results)

| Command | Meaning | Effects |
|---|---|---|
| `ufbt` | Build the app in the current directory (contains `application.fam`) into `dist/` | Executes project and SDK build code; may bootstrap |
| `ufbt lint` | `lint.py check`: clang-format/naming check | Formatting evidence only [C-BUILD-001] |
| `ufbt format` | `lint.py format`: rewrites formatting/naming | Mutates sources; review the diff |
| `ufbt update --branch=1.5.1` | Select SDK by release tag | Downloads; changes SDK state [C-BASE-004] |
| `ufbt update --url=<verified-url>` / `--local=<path> --hw-target=f7` | Fixed SDK URL or a local, already verified SDK archive (`--local` needs `--hw-target`) | Verify archive SHA-256 first [C-BASE-003] |
| `ufbt vscode_dist`, `ufbt create APPID=<id>` | Editor config / official template | Writes project files |
| `./fbt build APPSRC=applications_user/<path>` | Build an app in a prepared firmware tree, no upload | Firmware build; first run may fetch toolchain [C-BUILD-003] |
| `./fbt fap_<appid>` | Build a named external app in the firmware tree | Same |
| `./fbt FIRMWARE_APP_SET=unit_tests updater_package` | Build on-device unit-test firmware package | Does not run host tests [C-TEST-001] |
| `ufbt launch`, `ufbt flash`, `ufbt flash_usb`, `./fbt flash`, device `unit_tests` | Deploy, flash or run on hardware | **Separate explicit authority**; never part of an automatic check |

There is no `ufbt test` [C-BUILD-001]. Do not describe lint as static analysis.

## Isolation and identity

- Set `UFBT_HOME` to a project-specific directory (POSIX: `UFBT_HOME=$PWD/.ufbt-1.5.1 ufbt ...`; PowerShell: `$env:UFBT_HOME = "$PWD\.ufbt-1.5.1"`) so a global SDK is not mutated [C-BASE-004].
- Pin the official toolchain too (1.5.1 uses Flipper toolchain 39, gcc 12.3.1) [C-BUILD-005]. A distro `arm-none-eabi` toolchain changed the generated SDK API from 88.2 to 89.0 in a firmware-tree build [C-BUILD-004].
- Record: bootstrap version, SDK URL + SHA-256 + API, firmware commit, compiler/binutils versions, build flags/mode, host OS, app commit, dependency pins and `dist/*.fap` SHA-256.
- Repeatable inputs are not bit-for-bit reproducibility; claim the latter only after comparing hashes from two clean builds. Observed: single-source FAPs were identical across clean builds, multi-source FAPs varied with object order.

## API migration

1. Obtain both `api_symbols.csv` tables from pinned sources (or SDK archives).
2. Compare with the repository's `scripts/compare_api.py --old OLD --new NEW`; it reports version, removed/added exports and full signature changes, and exits 1 on selected regressions.
3. Map each removed/changed symbol to call sites; check macros/inline helpers in headers separately (absent rows prove nothing about them).
4. Rebuild against the target SDK; the official build and loader are authoritative for imports.
5. Community-only APIs (fork extensions) are a lineage change, not a version bump [C-FORK-001].

Observed example: 1.5.1 (API 88.2) → dev `3fb1d51` (API 88.4) adds 42 exports with no removals [C-BASE-005].

## Debugging crashes

Collect firmware/app/SDK identity, the matching ELF, the crash message (`furi_check`/`furi_crash` text such as "out of memory" or "memmgt in ISR" [C-MEM-002]), backtrace, heap/stack metrics and a reproduction before changing code. Do not raise stack size at random. SWD/GDB needs a supported probe and firmware/ELF matching the SDK; debugging halts firmware and changes timing [C-DBG-001].

## Host tests and emulation

Extract pure logic (parsers, protocol framing, state machines) and test it on the host with sanitizers where available. Mocks prove logic under modeled behavior only. Official firmware unit tests run on-device [C-TEST-001]. Community emulators are experimental and not comprehensive [C-EMU-001].

## Catalog and release

The pinned catalog bundler (`tools/bundle.py`) runs `ufbt update`, clones the source and submodules, and normally lints and builds; `--nolint`/`--nobuild` do not make it offline. Its requirements include dataclass-wizard, Pillow, PyYAML, Markdown, uFBT and requests [C-CAT-001]. The catalog `AGENTS.md` asks for 512×256 qFlipper-palette screenshots while the bundler accepts 4× or 8× scale factors; follow the stricter documented workflow [C-CAT-002]. Prepare submissions locally; publishing is a separate external action.

Release checklist: evidence report ([validation-report.md](../assets/validation-report.md)), artifact hashes, supported target/API, known limits, pending device tests ([device-test-plan.md](../assets/device-test-plan.md)), license of every bundled file.
