# .fap build reproducibility: root causes — 2026-10-16 (issue #17)

Host: Ubuntu 24.04 x86_64 (cloud container), Python 3.13.16, host gcc 13.3.0 and GNU binutils 2.42. The root-cause sections below are source inspection and host-toolchain analogues. The SDK confirmation at the end was run later the same day, after the network policy allowed the download. No device was used.

| Field | Result |
|---|---|
| Source inspection (fbt 9e810dc, uFBT 0.2.6) | pass: both causes located, see below |
| Host analogue (host gcc/binutils, Python) | pass: both mechanisms reproduced |
| SDK build with the recipe (five clean builds, a second path, a seed control) | pass: see [SDK confirmation](#sdk-confirmation-2026-10-16) |
| Bit-for-bit reproducibility, same paths, `PYTHONHASHSEED=0` | **observed** for all six fixtures across five clean builds |
| Bit-for-bit reproducibility across app folders | **not achieved**, upstream behavior: only `.gnu_debuglink` differs |

## Cause 1: object order follows Python's hash seed

`GatherSources()` in [`scripts/fbt_tools/sconsrecursiveglob.py`](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/scripts/fbt_tools/sconsrecursiveglob.py) starts with `sources_list = list(set(Flatten(sources_list)))`. `fbt_extapps.py` calls it with `[app.sources, "!lib"]`, then passes the result to `Program()` in that order, so the manifest's patterns are compiled and linked in set-iteration order. For `str`, that order depends on the per-process hash seed, which Python randomizes unless `PYTHONHASHSEED` is set. The fbt source is the authority here; the observations below only replay it.

- uFBT 0.2.6 starts SCons with `os.system(...)` ([`ufbt/__init__.py`](https://github.com/flipperdevices/flipperzero-ufbt/blob/v0.2.6/ufbt/__init__.py), `ufbt_cli`), after sourcing `fbtenv.sh`. Neither sets or clears `PYTHONHASHSEED`, so a value set by the caller reaches the SCons process.
- With one include pattern there is only one item to order. SCons sorts the result of each glob (`Dir.glob` returns `sorted(result, key=str)` in SCons 4.7.0's `SCons/Node/FS.py`). Toolchain 39 bundles SCons 4.7.0 (`ufbt --version`), so this applies.
- Sorting the `sources` list in `application.fam` has no effect, because the set discards list order.

This matches the qualification record exactly. The single-source apps never varied. The two multi-source apps (`sources=["soil_uart.c", "soil_frame.c"]` and `["ir_viewer.c", "ir_parser.c"]`) showed one of two hashes. Run D's log compiles `soil_frame.c` before `soil_uart.c`, which is the reverse of the manifest.

Replay of that expression (`source_order.py`, run with `python3 -s`; `-I` also ignores `PYTHONHASHSEED`):

```
unset seed, 40 processes:          19 soil_frame.c,soil_uart.c   21 soil_uart.c,soil_frame.c
PYTHONHASHSEED=0, 10 processes:    10 soil_frame.c,soil_uart.c
PYTHONHASHSEED=1, 10 processes:    10 soil_uart.c,soil_frame.c
PYTHONHASHSEED=2, 10 processes:    10 soil_frame.c,soil_uart.c
PYTHONHASHSEED=5, 10 processes:    10 soil_uart.c,soil_frame.c
```

A fixed seed gives a fixed order for one Python build. The toolchain's own `python3` runs SCons, so the order for a given seed is only comparable between hosts that use the same toolchain.

Host analogue showing that link order alone changes a stripped image (`host_analogue.sh`):

```
link order a.o b.o:  8b064217f03a05fc
link order b.o a.o:  e80c576de750a5ed
```

## Cause 2: the build path reaches the .fap through `.gnu_debuglink`

`generate_embed_app_metadata_actions()` in [`fbt_extapps.py`](https://github.com/flipperdevices/flipperzero-firmware/blob/9e810dc399e027e48ce07751df72168abbb19065/scripts/fbt_tools/fbt_extapps.py) makes the `.fap` from the debug ELF with `objcopy --strip-debug --strip-unneeded --add-gnu-debuglink=${SOURCE}`. Debug sections are stripped, but `.gnu_debuglink` holds the debug ELF's file name **and a CRC-32 of the whole debug ELF**. The debug ELF's DWARF records absolute paths: `DW_AT_comp_dir`, source paths, and SDK header paths under `UFBT_HOME`. So any change to the app folder or `UFBT_HOME` path changes the CRC and the `.fap` hash, even when the code is identical. That is consistent with the wizard's single-source hash (`07bd25b7…`, built in a random `mkdtemp` folder) differing from the qualification's (`2d2c389c…`, `/tmp/fred-sdk/apps/fred_tally`); the two debug ELFs were not kept, so this is inferred.

The other packaging steps were read for hidden inputs and none was found. `fastfap.py` builds its `.fast.rel*` sections from ordered dicts of ELF relocations. `.fapmeta` comes from the manifest, target and SDK version. `furi/core/check.h` uses no `__FILE__`. App code that uses `__FILE__` would add the path to `.rodata` as well.

Host analogue (`host_analogue.sh`, same packaging command, `--build-id=none`; host gcc enables a build ID by default):

```
two paths, default flags:      26c7a28874834654 aba075e8b2cb3701
  .gnu_debuglink A:   app_d.elf\0\0\0 + CRC 50699cb6
  .gnu_debuglink B:   app_d.elf\0\0\0 + CRC 4d37d494
  same, no debuglink:          2a15f7c55e4ed5e3 2a15f7c55e4ed5e3
two paths, -ffile-prefix-map:  ccbe70830bb54f06 ccbe70830bb54f06
same path, two clean builds:   26c7a28874834654 26c7a28874834654
```

Only `.gnu_debuglink` differs between the two paths. Without it, or with `-ffile-prefix-map`, the outputs match. uFBT 0.2.6 has no supported way to add app compiler flags: `App.cflags` does not exist, `Library.cflags` applies only to `fap_private_libs`, and the remaining hooks (`fbt_hooks.py`) need an edited SDK tree. So `-ffile-prefix-map` is not part of the recipe.

## Recipe

To compare `.fap` hashes between two builds:

1. Use the same uFBT, SDK archive and toolchain (39).
2. Build from the same absolute app folder with the same absolute `UFBT_HOME`.
3. Set `PYTHONHASHSEED` to the same value (FRED uses `0`) in the environment that runs `ufbt`.
4. Remove `UFBT_HOME/build` and the app's `dist/` before each build.

Observed with the SDK (below): identical hashes for every fixture, multi-source included, across clean builds. Paths still matter by design, because of cause 2. As a diagnostic, two `.fap` files that differ only in `.gnu_debuglink` can be compared with `objcopy --remove-section .gnu_debuglink` applied to copies. That is a comparison aid, not a release artifact.

Applied in this change: `evaluations/harness/build_fixtures.sh` exports `PYTHONHASHSEED=0` (unless set) and prints its paths. `scripts/fred_setup.py build` passes `PYTHONHASHSEED=0` to uFBT, and the sample build at the end of `sdk` now uses `<sdk home>/build/<app>` instead of a random temporary folder.


## SDK confirmation (2026-10-16)

Same cloud container, after the network policy was changed to allow `update.flipperzero.one`. `python3 scripts/fred_setup.py sdk --yes` installed uFBT 0.2.6, SDK 1.5.1 for f7 (archive sha256 `dfde39df…2ffe`, matching `compatibility.json`, API 88.2) and toolchain 39 (`arm-none-eabi-gcc` 12.3.1, SCons 4.7.0). Each run removed `UFBT_HOME/build` first, and `build_fixtures.sh` recreates the app folders and `dist/`. Every build and `ufbt lint` exited 0 with 0 warnings. Raw output is in [`sdk-runs/`](sdk-runs/).

| Fixture | Sources | Runs 1–5, `/tmp/fred-sdk`, seed 0 | Other folder `/tmp/fred-sdk-other`, seed 0 | Seed 1, `/tmp/fred-sdk` |
|---|---|---|---|---|
| fred_tally | 1 | `373ac9af…8372` ×5 | `487153f7…3317` | same as seed 0 |
| fred_interval_timer | 1 | `7fd880c1…2921` ×5 | `2ffa78bc…d8db` | same as seed 0 |
| fred_soil_uart | 2 | `7e7d87e2…e441` ×5 | `6cc0b6bd…169a` | `1f0004d3…8c91` |
| fred_ir_viewer | 2 | `923076e1…e798` ×5 | `f5c940a7…b1bf` | `0e6ba9e9…8253` |
| uart_logger_fixed | 1 | `91e9bd36…446d` ×5 | `90f03b7a…9d7f` | same as seed 0 |
| uart_logger_eval | 1 | `c377acae…3f9a` ×5 | `07222823…e389` | same as seed 0 |

- **Cause 1 confirmed.** With the seed fixed, the two multi-source apps gave one hash across five clean builds (the qualification saw two hashes in four builds). Changing only the seed to 1 changed only those two apps, and their build logs show the reversed compile order (`sdk-runs/compile-order.txt`).
- **Cause 2 confirmed.** Every fixture's hash changes in another folder. After `arm-none-eabi-objcopy --remove-section .gnu_debuglink` on copies, all six pairs are identical (`sdk-runs/debuglink-stripped.txt`), so `.gnu_debuglink` (debug ELF name plus CRC) is the only difference. Its CRC differs between the two folders for every app.

Result: the recipe gives bit-for-bit identical `.fap` files across clean builds when toolchain, SDK, uFBT, absolute paths and `PYTHONHASHSEED` are fixed. Across folders it does not, and that limit is upstream fbt packaging (C-BUILD-007), recorded in `build-debug-release.md`. One host and one toolchain build were used; other hosts were not compared.
