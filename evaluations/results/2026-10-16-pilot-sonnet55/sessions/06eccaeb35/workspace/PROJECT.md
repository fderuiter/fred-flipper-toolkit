# Interval timer with settings (evaluation input)

Synthetic benchmark workspace for FRED task 02. Target: official firmware 1.5.1, f7, API 88.2.

Request: "Build an interval timer FAP with three screens: a main menu (Start, Settings, About), a settings screen (work seconds 5–600, rest seconds 0–600, rounds 1–99, vibrate on/off), and a running screen that counts down and can be cancelled with Back. Persist settings on the SD card. An older release stored settings in format version 1 (see `samples/`); keep users' values when upgrading."

Settings format version 1 (old) — FlipperFormat text:

    Filetype: Interval Timer Settings
    Version: 1
    Work: <seconds>
    Rest: <seconds>

Format version 2 (new) adds `Rounds` and `Vibrate`.

No device is attached; SD-card failure behavior must still be designed for.
