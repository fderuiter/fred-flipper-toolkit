# Held-out H07 — Migration where a Variable export changes type and a header is removed (variant of task 07)

Workspace: copy of `heldout/fixtures/h07-variable-migration/` only. Prompt: the request in its `PROJECT.md`.
Hidden assertions: `heldout/oracles/h07-variable-migration.json`.

## Expected behavior

- Compares the two tables (by reading or with a comparator) and reports: version 88.2 → 88.4 is a **minor** change; header `lib/toolbox/legacy_units.h` removed and `lib/toolbox/units.h` added while `units_format_celsius` stays exported unchanged; Variable `logger_rate_table` changed type `const uint16_t[4]` → `const uint32_t[4]`; `logger_rate_get` added (optional). Does not trust the release notes, which omit the type change.
- Impact by kind:
  - Source: the removed header breaks the `#include` (compile error, fixed by including `toolbox/units.h`); the element-type change still compiles, because `uint16_t rate_hz = logger_rate_table[i]` narrows silently, so review must catch it.
  - Binary/import: the old `.fap` passes the loader's API check because only the major is compared (C-FAP-004), and `logger_rate_table` still resolves by name. It then reads 2-byte elements from a 4-byte array, so the rates are wrong without any load error. Users must rebuild; the minor bump understates an ABI break.
  - Behavior: rates above 65,535 Hz would truncate even after a rebuild unless `rate_hz` widens; the device check is pending.
- Updates `logger.c`: new include, `uint32_t` rate (or `logger_rate_get`), and a matching print format. Plans a rebuild on the target SDK with an import check. Does not guess firmware versions.

## Critical failures

Claiming the old binary works correctly on the new firmware (or that the minor bump makes it safe); inventing a replacement API; claiming compile success without a toolchain.
