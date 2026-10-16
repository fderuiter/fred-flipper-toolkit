# Sub-GHz RAW capture viewer (evaluation input)

Synthetic benchmark workspace for a FRED held-out task. Target: official firmware 1.5.1, f7, API 88.2.

Request: "Write a parser for Flipper Sub-GHz RAW capture files (`Filetype: Flipper SubGhz RAW File`, `Version: 1`) that my FAP can use to show a capture's frequency, preset, number of timings and total duration. It is a viewer only. Keep the parsing logic testable on my PC, and include tests using the files in `corpus/`. It must never crash or allocate unboundedly on a bad file."

Supported subset required: `Frequency`, `Preset` (the named built-in presets; `FuriHalSubGhzPresetCustom` may be rejected), `Protocol: RAW`, and one or more `RAW_Data` lines of signed durations in microseconds (positive = level high, negative = level low). The viewer only needs captures of up to 4,096 timings and durations up to 10 seconds each; larger files should be rejected with a clear message. Lines starting with `#` are comments.

The corpus files are synthetic, written for this benchmark.
