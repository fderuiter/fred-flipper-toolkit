# Storage, parsers and settings

Load when an app reads or writes files, parses native or custom formats, or persists settings.

## Storage behavior

- Storage calls block until the storage service completes them (`S_API_EPILOGUE` in `storage_external_api.c`); never call them in draw/input/timer/IRQ callbacks [C-STO-001].
- `storage_file_read`/`storage_file_write` return the bytes actually transferred: a short count is an error or a continuation, never success. Check `storage_file_open`, `storage_file_sync`, `storage_file_close` and `storage_common_rename` results [C-STO-001].
- Handle SD card absent, full, removed mid-operation, read-only, and slow. Show an actionable error; keep the app responsive and exitable.
- Use the app data folder (`APP_DATA_PATH(...)`/`/data` for FAPs) rather than inventing absolute paths; reject path components from untrusted input (`..`, `/`, control characters).

## Parsers (native and custom formats)

Native `.sub`, `.ir`, `.nfc`, `.rfid`, `.ibtn` are distinct schemas [C-FMT-001]. A matching `Filetype`/`Version` header does not validate the body. Prefer FlipperFormat for keyed native formats; it is exported (`flipper_format_*`).

Bound every dimension:

- maximum line/token length and total file size or record count processed;
- integer parse with range checks (no silent wrap, no `atoi`);
- `count × element_size` and `offset + length` overflow checks before allocation or indexing;
- duplicate, missing and unknown keys: decide reject, ignore or last-wins explicitly;
- truncated final record; non-printable bytes in names;
- total work per input so a hostile file cannot stall the UI.

Example: Infrared `.ir` v1 (`Filetype: IR signals file`, `Version: 1`) has `parsed` signals (`protocol`, 4-byte `address`/`command`) and `raw` signals (`frequency`, `duty_cycle`, `data` timings, documented maximum 1024 timings) [C-FMT-001]. A bounded subset parser states which fields it supports and rejects the rest.

Raw Sub-GHz `.sub` data is timing data, not raw IQ samples.

Extract parsing logic into plain C with no Furi dependencies so it can be host-tested with a malformed-input corpus; device testing remains separate [C-TEST-001].

## Settings

1. Store an app-specific format identifier and version.
2. Load: validate every field's range; fall back to defaults per field or wholesale (decide which).
3. Known older version → migrate through validated state.
4. Unknown future version → do not overwrite silently; choose read-only, reset-with-notice or refuse, and document it. Read the version before full validation, and treat an existing file you cannot parse the same way: never let a later save rotate or delete it without the user's action.
5. Save: write a temporary file, check every write length and the sync/close result, then replace via the storage API; keep a recoverable previous copy when losing settings would matter.
6. Startup: detect stale temporary files from interrupted saves and recover deterministically.

Do not describe rename/sync as power-loss atomic; that is unresolved for this filesystem and device [C-STO-002]. Put file operations behind a small interface so host tests can (a) read the real sample files the user supplied and (b) inject a failure at each write/sync/rename/remove step and check that the next load recovers old or new settings. Device behavior stays pending until tested on hardware.
