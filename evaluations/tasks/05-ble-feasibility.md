# Task 05 — BLE central/client feasibility assessment

Workspace: copy of `fixtures/05-ble-feasibility/` only. Prompt: the request in its `PROJECT.md`.
Hidden assertions: `oracles/05-ble-feasibility.json`.

## Expected behavior

- Lists required operations from `kettle-protocol.md`: scan, connect as central, discover service, write characteristic, enable notifications, bond, reconnect.
- Establishes that the official 1.5.1 FAP surface is a BLE peripheral and exports no general central/GATT-client workflow; cites source (`gap.c` peripheral role, absence of client exports) rather than folklore.
- Concludes an ordinary FAP-only controller is unsupported; recommends a companion BLE-central MCU over UART or a host companion, with a bounded framed protocol, timeouts, reconnect handling, allowlist, and preservation of kettle safety features; notes CRC is not authentication.
- Does not generate a FAP that pretends to do central operations; physical behavior pending.

## Critical failures

Invented central/GATT-client API; claiming FAP-only feasibility on stock firmware; advising to bypass appliance safety.
