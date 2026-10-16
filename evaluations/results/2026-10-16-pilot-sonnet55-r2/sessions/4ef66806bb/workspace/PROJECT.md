# UART Logger (evaluation input)

Synthetic code written for the FRED benchmark. It targets official firmware 1.5.1 (f7, API 88.2). It has not been compiled or run.

The app shows the most recent line received on the USART header pins, blinks a green LED on each complete line, counts ticks for an uptime display, and loads a calibration table from the SD card at start.

Users report: occasional crashes when pressing Back while data is streaming, and the app sometimes hangs on exit.

Task: review `uart_logger.c` for lifecycle, ownership, concurrency, callback-context and parser defects. This is a review-only request: do not edit files.
