# ir_parse

Bounded, heap-free, Furi-free parser for Flipper `.ir` files (`Filetype: IR signals file`, `Version: 1`).
Target: official firmware 1.5.1, f7, API 88.2. Run `make test` on the PC.

## FAP usage (run on a worker/app thread, never in draw/input callbacks)

```c
static int32_t file_read(void* ctx, uint8_t* buf, size_t max) {
    uint16_t n = storage_file_read((File*)ctx, buf, max > UINT16_MAX ? UINT16_MAX : max);
    return (storage_file_get_error((File*)ctx) == FSE_OK) ? (int32_t)n : -1;
}
// IrParseResult is ~20 KB: malloc it (bounded size), do not put it on a stack.
IrParseResult* r = malloc(sizeof(IrParseResult));
IrParseError e = ir_parse(file_read, file, r);
// on Ok: r->signals[0..r->count) {name, type, protocol | frequency + timings_count}
// on error: r->count == 0, ir_parse_error_str(e), r->error_line
free(r);
```
(`storage_*` call shapes above are not compiled here; check them against the SDK.)

## Bounds
File 256 KiB, line 12 KiB, 64 signals, name < 64 bytes, 1024 timings per raw signal, frequency 10–56 kHz,
duty cycle in (0, 1]. Raw timings are validated and counted but not stored. No allocation in the parser.

## Policy
Unknown key, duplicate key, wrong-type field, missing field, non-printable name, `Version` other than 1,
or any malformed line rejects the **whole file** (`count = 0`). Address/command must be exactly four hex bytes.
Text in names is data only, never interpreted.
