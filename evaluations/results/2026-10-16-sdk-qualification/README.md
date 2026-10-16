# SDK qualification record — 2026-10-16 (issue #7)

Host: Ubuntu 24.04.5 LTS x86_64 (cloud container). No device was attached; no `launch`, `flash`, device discovery, GPIO or transmission was performed.

## Identities

| Item | Value | Verification |
|---|---|---|
| Official update index | `https://update.flipperzero.one/firmware/directory.json`, snapshot SHA-256 `6062f36d479b2d5fe8fdc0a9dd6ceeeab15f2050d97df6dd7a7ac51090211fac` | Identical to the snapshot recorded during research |
| f7 SDK archive | `https://update.flipperzero.one/builds/firmware/1.5.1/flipper-z-f7-sdk-1.5.1.zip`, 19,604,363 bytes | **SHA-256 `dfde39df32ba8a9789b26afc1d702579ad8399a1c285770637cfe3287b2e2ffe` matches the published digest** |
| SDK API table | `sdk_headers/f7_sdk/targets/f7/api_symbols.csv`, SHA-256 `5c9505984a19be218c19400d43ab9c7056996d003e69809945d9db03e95c04bb` | Byte-identical to `targets/f7/api_symbols.csv` at firmware commit `9e810dc` (API 88.2); `compare_api.py` reports no differences |
| uFBT bootstrap | 0.2.6 from PyPI, wheel SHA-256 `ee3b5788b4034694aea594fd44777eab4049968d94c5f39ad93df8b05d8c8849` | Matches PyPI's published digest; installed in an isolated venv |
| SDK selection | `UFBT_HOME=<isolated dir> ufbt update --local=<verified zip> --hw-target=f7` | `--hw-target` is required with `--local` (observed error otherwise) |
| Toolchain | Flipper toolchain version 39: `arm-none-eabi-gcc (GCC) 12.3.1 20230626`, `GNU ld (GNU Binutils) 2.40.0.20230627`; archive `https://update.flipperzero.one/builds/toolchain/gcc-arm-none-eabi-12.3-x86_64-linux-flipper-39.tar.gz` | Downloaded by uFBT on first build. No publisher checksum is offered (`.sha256` URL returns 404); an independent download had SHA-256 `c044c59233f66f6efcf854affe2d5e09dbad04d68709d513bc760b1d98895853` (observed, not publisher-verified) |
| Build mode | uFBT default (debug ELF plus `.fap`), target f7 | — |

Build hooks: every designated `application.fam` was read before building. All are plain `App(...)` declarations written for this repository, with no `fap_extbuild` or other hooks.

## Designated fixtures and results

Command: `evaluations/harness/build_fixtures.sh` (runs `ufbt` then `ufbt lint` in each app directory). Results are from the final clean run D. Logs are in `logs/`.

| App | Source | Build | Compiler warnings | `APPCHK` | `ufbt lint` (formatting/naming only) |
|---|---|---|---|---|---|
| `fred_tally` | reference 01 | exit 0 | 0 | Target 7, API 88.2 | pass |
| `fred_interval_timer` | reference 02 | exit 0 | 0 | Target 7, API 88.2 | pass |
| `fred_soil_uart` | reference 03 | exit 0 | 0 | Target 7, API 88.2 | pass |
| `fred_ir_viewer` | reference 04 | exit 0 | 0 | Target 7, API 88.2 | pass |
| `uart_logger_fixed` | reference 06 (corrected) | exit 0 | 0 | Target 7, API 88.2 | pass |
| `uart_logger_eval` | fixture 06 (agent input, deliberately flawed) | exit 0 | 0 | Target 7, API 88.2 | pass |

Before formatting, the first run's lint failed on all six apps with `-Wclang-format-violations`. `ufbt format` rewrote the sources and the formatted versions are committed. The task 06 agent input is part of this; no pilot session had used it before the change.

Not built: task 07. Its API tables are deliberately synthetic, so a migrated `meter.c` cannot target the real SDK; it is covered by `compare_api.py` tests instead. Task 05 is a feasibility task with no code.

`uart_logger_eval` compiling cleanly is expected. Its eight seeded defects are lifecycle and concurrency errors that a compiler does not detect, which is the point of task 06.

## Repeatability

Four clean builds (A–D; `UFBT_HOME/build` removed before each run), `.fap` SHA-256 prefixes:

| App | A | B | C | D |
|---|---|---|---|---|
| fred_tally | 2d2c389c55db | 2d2c389c55db | 2d2c389c55db | 2d2c389c55db |
| fred_interval_timer | eaa277ac22de | eaa277ac22de | eaa277ac22de | eaa277ac22de |
| fred_soil_uart | c494135f0fbe | c494135f0fbe | a8bceaa80737 | a8bceaa80737 |
| fred_ir_viewer | 4d8375b1d1c9 | f6f8314a01d4 | f6f8314a01d4 | f6f8314a01d4 |
| uart_logger_fixed | d9c564429cee | d9c564429cee | d9c564429cee | d9c564429cee |
| uart_logger_eval | 97855730f0b7 | 97855730f0b7 | 97855730f0b7 | 97855730f0b7 |

Single-source apps were byte-identical in every run. The two multi-source apps produced one of two outputs: the build logs show the two objects compiled (and linked) in different orders between runs, which changes the section layout. Inputs are repeatable, but **bit-for-bit reproducibility is not established** for multi-source apps.

## Host tests

Command: `sh evaluations/reference/run_host_tests.sh`. The reference logic for tasks 02, 03 and 04 is run against the agent-facing fixture inputs, under gcc 13.3.0 (`-O0` with ASan/UBSan, and `-O2`) and clang 18.1.3 (`-O0`, `-O2`; clang's sanitizer runtime is not installed on this host).

| Suite | Cases | Result |
|---|---|---|
| 02 settings | v1 migration, v2 load, corrupt → defaults, future version detected and file untouched, round trip, out-of-range rejected, crash injected at each of the 4 mutating save steps (+ none) recovers old or new settings, missing file, 8×N bit-flip fuzz | 12/12 configurations pass |
| 03 framing | CRC check value, `burst_hex.txt` → 4 readings, `noise_hex.txt` → 1 reading with ≥2 counted errors, 200,000 random bytes | pass |
| 04 `.ir` parser | 12 corpus files match the oracle, every truncation prefix of the two valid files, 1,024 maximal timings accepted | pass |

Defects found and fixed during qualification, all in reference code or tests (none in the toolkit content):

- The first `.ir` parser used one 256-byte line limit, which would have rejected legitimate raw signals near the documented 1,024-timing maximum. It now uses a per-key limit.
- The frame parser's resync was recursive, with stack depth up to one frame. It is now iterative and bounded.
- The settings test compared structs with `memcmp`, which reads indeterminate padding bytes. Clang `-O2` exposed this; the test now compares field by field.

## Non-official toolchain experiment

Before the SDK host was reachable, the pinned firmware tree was built with Ubuntu's `arm-none-eabi-gcc 13.2.1` and newlib 4.4. The SDK API generator then dropped ten `__retarget_lock_*` exports, and `fbt` rewrote `targets/f7/api_symbols.csv` to version **89.0**. Apps built that way would declare an API that 1.5.1 firmware does not provide. That scratch tree was discarded and nothing from it is used here. This is the concrete reason FRED requires pinning the official toolchain, not only uFBT and the SDK (claim `C-BUILD-004`).

## What this does not show

The UI, timing, UART, storage and settings behaviour has not been checked on a device (#9). Compilation and host tests are reported separately from device behaviour.
