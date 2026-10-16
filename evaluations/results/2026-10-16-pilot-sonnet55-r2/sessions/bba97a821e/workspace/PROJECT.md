# Infrared remote viewer (evaluation input)

Synthetic benchmark workspace for FRED task 04. Target: official firmware 1.5.1, f7, API 88.2.

Request: "Write a parser for Flipper `.ir` remote files (Filetype `IR signals file`, Version 1) that my FAP can use to list buttons and show each signal's type, protocol or frequency, and number of raw timings. Keep the parsing logic testable on my PC, and include tests using the files in `corpus/`. It must never crash or allocate unboundedly on a bad file."

Supported subset required: `name`, `type` (`parsed` or `raw`), `protocol`, `address`, `command`, `frequency`, `duty_cycle`, `data`. Other keys may be rejected.

The corpus files are synthetic, written for this benchmark.
