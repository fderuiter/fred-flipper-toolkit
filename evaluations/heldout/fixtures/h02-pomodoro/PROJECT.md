# Pomodoro timer with settings (evaluation input)

Synthetic benchmark workspace for a FRED held-out task. Target: official firmware 1.5.1, f7, API 88.2.

Request: "Build a Pomodoro FAP with three screens: a main menu (Start, Settings, About), a settings screen (work minutes 1–90, short break minutes 1–30, long break minutes 1–60, work sessions before a long break 1–12, sound on/off), and a running screen that counts down work and break phases and can be cancelled with Back. Persist settings on the SD card. Two older releases stored settings in format versions 1 and 2 (see `samples/`); keep users' values when they upgrade."

Settings format history (FlipperFormat text, `Filetype: Pomodoro Settings`):

- Version 1: `Work`, `Break` (minutes).
- Version 2: `Break` was renamed to `ShortBreak`; `LongBreak` was added.
- Version 3 (new): adds `Cycles` (work sessions before a long break) and `Sound` (bool).

No device is attached; SD-card failure behavior must still be designed for.
