# Door Monitor (evaluation input)

Synthetic code written for a FRED benchmark. It targets official firmware 1.5.1 (f7, API 88.2). It has not been compiled or run.

The app watches a reed switch on header pin 2 (PA7, input with pull-up). It has a menu, a monitor screen that shows how many times the door opened and how long the monitor has been armed, and an auto-disarm after the configured number of minutes. Alert presets are loaded from the SD card at start. A worker thread blinks the LED when the door opens.

Users report: the app sometimes freezes when auto-disarm triggers, crashes now and then when the door opens, and occasionally crashes on exit.

Task: review `door_monitor.c` for lifecycle, ownership, concurrency, callback-context and parser defects. This is a review-only request: do not edit files.
