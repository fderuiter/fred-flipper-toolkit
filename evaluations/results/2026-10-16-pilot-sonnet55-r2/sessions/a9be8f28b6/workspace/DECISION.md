# Kettle remote: feasibility decision

**Result: a FAP-only controller for the AcmeBrew K2 is not supported on official firmware 1.5.1. I did not write the FAP.**

## Compatibility card
| Item | Value |
|---|---|
| Firmware | official 1.5.1 (commit 9e810dc), f7, SDK API 88.2 [C-BASE-001/002] |
| Surface requested | external native FAP |
| Hardware / toolchain | unknown (not stated); only blocks build and test, not this decision |
| Authorized actions | none stated; no appliance commands sent |

## Required operations (from kettle-protocol.md)
1. Scan for the `K2-xxxx` peripheral. This needs the **central/observer** role.
2. Connect and discover primary service `ff00`. This needs a **GATT client**.
3. Write `ff01` and enable notifications on `ff02` via the CCCD. This needs GATT client write and subscribe.
4. Bond on first connection, then reconnect. The kettle drops idle links after 30 s, so the app needs keep-alive and reconnect logic.

## Why a FAP can't do it
At 9e810dc the Flipper BLE stack is configured as a peripheral only (`gap_init` uses `GAP_PERIPHERAL_ROLE`). The central and GATT-client calls (`aci_gap_create_connection`, `aci_gatt_disc_*`, `aci_gatt_read_char_*`, `aci_gatt_write_char_*`) are not exported to external apps [C-BLE-001] [C-BLE-002]. The public `ble_gatt_*` APIs only build GATT servers. Operations 1–3 are therefore missing, and I won't invent APIs to cover them. This is a limit of the public SDK, not a claim that the radio hardware can't do it.

## Surfaces compared
| Surface | Verdict |
|---|---|
| Native FAP, official 1.5.1 | Rejected: no central or GATT client. |
| JavaScript | Rejected: its BLE module covers only the peripheral/GATT-server side, and it is a subset runtime (feasibility-level, not verified here). |
| Built-in app or firmware or radio-stack change | Possible in principle. It needs custom firmware and CPU2, which is not "stock", and it is high-risk. Not recommended. |
| Fork firmware (Momentum, Unleashed) | Separate lineage, unverified, and not stock. Would need its own inspection [C-FORK-001]. |
| **Flipper UI plus a BLE-central companion MCU over UART** | **Recommended if "no phone" matters.** An ESP32 or nRF52 does the scan, connect, bond, CCCD and keep-alive. The FAP is a small UI (on/off, temperature 40–100) that sends framed commands over UART. |
| Host computer companion (laptop or Raspberry Pi) | Alternative. Same split, with the Flipper as a remote UI. |

## Plan for the recommended option
- Check that the chosen MCU supports BLE central and bonding, and that its UART is 3.3 V compatible with the Flipper header pins.
- Define a versioned UART frame with a request ID, length, CRC, timeouts and a target allowlist (`K2-xxxx`). A CRC detects corruption but is not authentication.
- The MCU builds the kettle frames `[0x55, cmd, value, checksum]`. The checksum algorithm isn't in the notes; you need to specify it.
- Clamp temperature to 40–100 °C in both the FAP and the MCU. Never replay stale commands after a reconnect, and show connection state in the UI.
- The kettle's boil-dry and lid protection stays in force. The remote must not try to bypass it.
- Sending real commands to the kettle needs your explicit go-ahead, and the behavior is untested until you try it on hardware.

## Open unknowns
- Checksum algorithm and bonding/pairing details.
- Which companion hardware you have.
- Whether "no phone" allows a small external board.

If you tell me which MCU or host you'll use, I can write the FAP UI and the UART protocol, plus the companion firmware.
