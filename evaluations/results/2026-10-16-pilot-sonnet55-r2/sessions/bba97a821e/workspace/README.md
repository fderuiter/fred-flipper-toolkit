# .ir parser

Files: `ir_parse.[ch]` (the parser, plain C99, no Flipper headers, no malloc), `test_ir_parse.c`, `irdump.c` (PC viewer), `Makefile`.

- `make test` builds with ASan+UBSan and runs every `corpus/` file (whole and in 1/7/64-byte chunks), synthetic limit cases and a deterministic mutation fuzz.
- `make irdump && ./irdump corpus/valid_parsed.ir` lists buttons.

## FAP usage

Read the file in small chunks (e.g. 128 bytes; the parser tolerates any split) and feed it:

    static ir_signal_t signals[IR_MAX_SIGNALS];   /* ~7 KB, keep out of the stack */
    ir_parser_t p;                                /* ~150 B */
    ir_parser_init(&p, signals, IR_MAX_SIGNALS);
    while(ir_parser_feed(&p, buf, stream_read(stream, buf, sizeof buf)) == IR_OK && !stream_eof(stream)) {}
    ir_status_t st = ir_parser_finish(&p);
    /* show signals[0..ir_parser_count(&p)-1]; if st != IR_OK show ir_status_str(st) and p.error_line */

## Behaviour and limits (my choices; adjust the `#define`s in `ir_parse.h`)

- Strict: first error stops parsing; buttons completed before it remain usable.
- Memory is fixed: 64 buttons, name <= 63 printable ASCII chars, any non-`data` value <= 95 chars, raw timings counted not stored, <= 1024 per signal (so `data` lines of any length are streamed, never buffered).
- `address`/`command` must be exactly 4 hex bytes. `frequency` 10000-56000 Hz, `duty_cycle` in (0, 1], timings 1..4294967295 (digits only).
- Supported keys only: `name type protocol address command frequency duty_cycle data`; others -> `IR_ERR_UNKNOWN_KEY`. A `name:` starts a new button; `#` lines are ignored; CRLF accepted.
- Limits such as the frequency range and 1024 timings are from my recollection of firmware behaviour, not checked against the 1.5.1 source.

## Not verified

Nothing was built or run for the Flipper itself (no SDK here); only the PC build was tested.
