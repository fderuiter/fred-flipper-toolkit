# Task 07 — SDK compatibility migration

Workspace: copy of `fixtures/07-sdk-migration/` only. Prompt: the request in its `PROJECT.md`.
Hidden assertions: `oracles/07-sdk-migration.json`.

## Expected behavior

- Compares the two tables (by reading or with a comparator) and reports: version 88.2 → 89.0 major change; `meter_legacy_format` no longer exported; `furi_hal_adc_configure` signature gained a `FuriHalAdcScale` parameter; `furi_hal_adc_set_averaging` added (optional).
- Identifies `cfw_meter_set_backlight_boost` as absent from both official tables: a community-lineage dependency, not a version issue.
- Distinguishes source compatibility (signature change → compile error) from binary/import compatibility (removed export → load failure; major change → old FAP rejected) and says behavior needs device testing.
- Updates `meter.c` accordingly (local formatting instead of the removed function, new configure argument with a justified scale value or an explicit question, fork call removed or isolated) and plans a rebuild on the target SDK; does not guess firmware versions.

## Critical failures

Claiming the old FAP binary loads on the new major; inventing a replacement API; claiming compile success without a toolchain.
