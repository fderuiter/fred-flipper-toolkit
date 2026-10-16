# Greenhouse humidity sensor (evaluation input)

Synthetic benchmark workspace for a FRED held-out task. Target: official firmware 1.5.1, f7, API 88.2. The sensor and its protocol are invented for this benchmark (`sensor-notes.md`).

Request: "Add support for my HX-40 humidity sensor to my FAP. It is on the external I2C pins of the GPIO header. Poll it every 2 seconds from a background thread and show the latest humidity and temperature plus an error count. After a power glitch the sensor sometimes holds the bus, so the app has to recover from a stuck bus by itself instead of hanging. Back must stop polling and exit cleanly."

The sensor board's supply voltage and whether it has its own pull-up resistors have not been confirmed. No device or sensor is attached.

`captures/` holds synthetic transaction logs decoded by a logic analyzer.
