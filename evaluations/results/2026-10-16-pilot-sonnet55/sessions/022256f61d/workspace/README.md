# Flipper `.ir` parser

`src/ir_parser.{c,h}`: portable C99, no heap, no Flipper SDK. Copy both files into the FAP.
`tests/test_ir_parser.c`: host tests over `corpus/` plus prefix/mutation sweeps. Run `make test` (ASan + UBSan).

FAP usage sketch (read from a `Stream`/`File` in chunks; nothing else is kept in memory):

```c
static irv_button_t buttons[64];
irv_result_t res;
irv_parser_t p;
irv_init(&p, &res, buttons, 64);
while((n = storage_file_read(file, buf, sizeof(buf))) > 0 && irv_feed(&p, buf, n)) {}
irv_finish(&p);                // res.count buttons valid; res.error / res.error_line if bad
irv_describe(&buttons[i], line, sizeof(line));   // "NEC" or "raw 38000Hz, 11 timings"
```

Bounds: name 31 printable-ASCII chars, 1024 timings/signal, 256 KiB/file, caller-sized button array,
~250 B parser state. On the first error parsing stops; earlier complete buttons stay usable.
Unsupported keys are rejected; frequency must be 10000-56000 Hz, duty cycle in (0, 1].
