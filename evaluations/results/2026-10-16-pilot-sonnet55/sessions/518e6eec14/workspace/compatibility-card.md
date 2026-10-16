# Kettle remote: feasibility decision

**Result: not feasible as a stock-Flipper, FAP-only app on official firmware 1.5.1. I did not write the FAP.** Any code would have had to call BLE central/GATT-client functions that the firmware does not export.

## Compatibility card

| Field | Value | Status | Consequence if unknown |
|---|---|---|---|
| User outcome | Turn the AcmeBrew K2 on and set its temperature over BLE, with no phone | from PROJECT.md | |
| Firmware | Official 1.5.1, commit `9e810dc`, f7 | user-stated; baseline [C-BASE-001] | |
| SDK API | 88.2 | source-confirmed [C-BASE-002] | |
| Execution surface | External FAP, requested | | Rejected below |
| Hardware | Stock Flipper, no companion MCU | user-stated | |
| Host OS / toolchain | Unknown | unresolved | Blocks only the build of the alternative |
| Authorized actions | Read and edit locally. Appliance commands and deploying to a device are not authorized yet. | | |

## Required operations vs. the stock FAP surface

The kettle protocol (`kettle-protocol.md`) needs the Flipper to be the BLE central, not a peripheral.

| Required operation | Available to a FAP on 1.5.1? |
|---|---|
| Scan for `K2-xxxx` | No exported central/scan workflow |
| Connect as central | No. `gap_init` sets `GAP_PERIPHERAL_ROLE` [C-BLE-001] (source-confirmed), and `aci_gap_create_connection` is not exported. |
| Discover service `ff00` | No. `aci_gatt_disc_*` is not exported [C-BLE-002]. |
| Write `ff01` | No. `aci_gatt_write_char_*` is not exported. |
| Enable notifications on `ff02` via the CCCD (required before the kettle accepts commands) | No GATT-client path |
| Bond on first connection, reconnect within the 30 s idle limit | Only peripheral-side pairing is exposed |

The `ble_gatt_*` functions that are exported are server-side. A custom GATT server or an advertised beacon does not change the Flipper's role, so the kettle cannot be driven that way.

This is an inference from the pinned API table [C-BLE-002, status `inferred`]. It is not a claim that the radio silicon cannot do central, and I did not test it on a device.

## Surfaces compared

- **Native FAP:** rejected, per the table above.
- **JavaScript on the Flipper:** rejected. It sits on the same BLE stack and has no central module [C-JS-001].
- **Custom firmware or CPU2 radio-stack change:** possible in principle, but it is a different project. It leaves official 1.5.1, needs recovery and debug planning, and is not "a FAP". Forks such as Momentum or Unleashed are separate lineages, and I haven't inspected them [C-FORK-001].
- **Host companion (laptop or Raspberry Pi doing BLE):** works, but it is a computer rather than the Flipper alone. It is not a phone, though.
- **Companion BLE-central MCU on the Flipper's GPIO header (recommended):** the Flipper provides the UI and talks to the MCU over UART. The MCU does the scan, connect, bond, CCCD enable and writes. Use a chip with verified central/client support, such as an ESP32 or nRF52, and confirm it works at 3.3 V. This keeps "no phone" and keeps the Flipper as the remote.

## Recommended plan (if you want to proceed)

1. **Flipper FAP** (flipper-native): a screen with on/off and a temperature setting limited to 40–100 °C. It sends framed requests over UART with a bounded, versioned format that has request IDs, lengths, a CRC and timeouts. It shows states such as connecting, bonded, ready and error. Teardown must stop the serial worker cleanly.
2. **MCU firmware:** scan for `K2-*`, bond, discover `ff00`, enable `ff02` notifications, and write `[0x55, cmd, value, checksum]` frames to `ff01`. It keeps the link alive against the 30 s idle drop, for example with a periodic status request. This part is not in scope for the Flipper SDK.
3. **Safety:** do not queue or replay stale commands after a reconnect. Treat the on/off command as explicit user intent. Rely on the kettle's own boil-dry and lid protections, and don't try to bypass them. The CRC is not authentication, so add a target allowlist, such as the specific `K2-xxxx` name or address.

## Unknowns that need you

- **Checksum algorithm:** `kettle-protocol.md` doesn't say how it is computed (sum, XOR, other?). This blocks the frame builder.
- **Pairing details:** the notes don't say what bonding needs (passkey, just-works, or something else).
- **Which path you want:** a companion MCU, a host computer, or a custom-firmware investigation. Tell me which and I'll scope it. If you pick the MCU route, tell me which chip you own.

## Validation status

Documentation and source inspection are recorded above. Compiled, host-tested and device-tested are all `not-run`: nothing was built, and no commands were sent to a kettle.
