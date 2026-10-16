# Flipper mJS fork host tests (2026-10-16)

Host build of the **core interpreter** from `lib/mjs` at official firmware 1.5.1 (`9e810dc`), compiled with gcc 13.3.0 on Ubuntu 24.04 x86_64 together with a small host harness (file loading and a `print` modeled on `js_str_print`). The firmware's `js_app` modules and globals (`parseInt`, `console`, `require`, modules) are not part of the core and were not exercised. **This is not on-device behavior.** Timing, float formatting and overflow printing come from x86 glibc and may differ on the device.

- `tests/` — the 78 scripts run.
- `host-results.txt` — the raw output of each script and its exit status.

The firmware source and the harness are not vendored here because the mJS sources are GPL-2.0/commercial. To rebuild, copy `lib/mjs` from the pinned tree, stub `furi.h` (assert, `UNUSED`, log macros) and replace `common/platforms/platform_flipper.c` with host file reading.

The key results were reproduced independently: closures over outer locals, `try`, `JSON`, `==`, implicit number/string conversion, and `Array.prototype.map` all fail. A summary is in [the expansion evidence note](../../2026-10-16-expansion-evidence.md#javascript-mjs-fork).
