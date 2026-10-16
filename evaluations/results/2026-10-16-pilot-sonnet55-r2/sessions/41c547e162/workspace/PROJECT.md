# Soil sensor link (evaluation input)

Synthetic benchmark workspace for FRED task 03. Target: official firmware 1.5.1, f7, API 88.2. The sensor protocol below is invented for this benchmark.

Request: "Add UART support to my FAP so it shows readings from my soil sensor board on the USART header at 115200 8N1. The board sends frames continuously and sometimes in bursts after it wakes up. Show the latest reading and an error count; Back must stop reading and exit cleanly."

Frame format (little endian):

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | sync byte 0xA5 |
| 1 | 1 | payload length N (0–32) |
| 2 | N | payload: u16 moisture (0–1000), i16 temperature (0.1 °C), remaining bytes reserved |
| 2+N | 2 | CRC-16/CCITT-FALSE over length and payload |

The sensor board's logic level and power requirements have not been confirmed. No device or sensor is attached.

Captures in `captures/` are synthetic hex dumps.
