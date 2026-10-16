# Kettle remote: feasibility decision

**Result: a FAP-only kettle controller is not supported on stock official firmware 1.5.1. I did not write the FAP.** The code would call APIs that don't exist, so it couldn't build or run. Details below.

## Compatibility card

| Field | Value | Source / status | Consequence if unknown |
|---|---|---|---|
| User outcome | Turn the AcmeBrew K2 on and set 40–100 °C over BLE, with no phone | PROJECT.md | n/a |
| Firmware | Official 1.5.1 (commit `9e810dc`), f7 | PROJECT.md, plus the skill's baseline [C-BASE-001] | n/a |
| Execution surface requested | External FAP | PROJECT.md | n/a |
| SDK API | 88.2 (research baseline) | [C-BASE-002] source-confirmed | n/a |
| Host OS / toolchain / board revision | unknown | not in the repo | Blocks nothing for this decision. It blocks only a later build. |
| Hardware available | Stock Flipper and kettle only. No companion MCU is mentioned. | PROJECT.md | Companion hardware would have to be bought or confirmed. |
| Authorized actions | read/edit only. No appliance commands have been authorized. | none stated | No kettle commands until the user authorizes them. |

## Capability decision

The kettle is a BLE peripheral. Controlling it needs the Flipper to act as a BLE central.

| Required operation | Flipper 1.5.1 public FAP API | Status |
|---|---|---|
| Scan for `K2-xxxx` | No exported central or scanner API | source-confirmed absent [C-BLE-002] |
| Connect as central | `gap_init` configures `GAP_PERIPHERAL_ROLE`. `aci_gap_create_connection` is not exported. | source-confirmed [C-BLE-001] |
| Discover service `ff00` | `aci_gatt_disc_*` not exported | source-confirmed absent |
| Write `ff01`, enable the CCCD on `ff02` | `aci_gatt_write_char_*` is not exported. Only server-side `ble_gatt_*` exists. | source-confirmed absent |
| Bond on first connect | No central-side pairing workflow is exposed | inferred |

A custom GATT server or beacon doesn't help, because the role is still peripheral. This is a limit of the public SDK, not a claim about the silicon. Changing the radio stack (CPU2) would be a separate, unscoped firmware project, and it's the wrong tool for a kettle remote.

## Surfaces compared

- **Native FAP alone:** rejected, because the central operations above are missing.
- **JavaScript:** rejected. It uses the same BLE stack, and its modules don't add a central role [C-JS-001].
- **Firmware fork or modification:** rejected for now. It's heavy and needs recovery planning. It also leaves you off official firmware, so the "stock" requirement no longer holds. Forks are separate lineages [C-FORK-001], and I haven't inspected any of them for central support.
- **Host companion (phone or computer):** works, but it conflicts with "no phone needed". A computer or Raspberry Pi would meet the "no phone" part.
- **Flipper UI plus a BLE-central companion MCU over UART:** **recommended.** An ESP32 is one candidate, but check its central/GATT-client support and 3.3 V compatibility first. A FAP then supplies only the UI and a framed UART link.

## Recommended plan, if you want to go ahead

1. **Companion MCU firmware** does the BLE work:
   - Scan for `K2-*`, connect, discover `ff00`, enable notifications on `ff02`, then bond.
   - Send frames `[0x55, cmd, value, checksum]` to `ff01`.
   - Reconnect on demand, because the kettle drops idle connections after 30 s.
2. **Flipper FAP** provides:
   - A UI with on/off and a temperature picker clamped to 40–100.
   - A UART link to the MCU with a bounded, versioned protocol: request IDs, lengths, CRC, timeouts, and explicit connect/bonding/error states.
   - A worker thread for UART RX, with RX callbacks kept minimal. Teardown follows engineering-rules §4.
3. **Safety:** keep the kettle's own boil-dry and lid protections as the authority. Never replay stale "on" commands after a reconnect. Show the connection state, and have the first command after a reconnect confirm the current state.
4. **Open protocol questions:** `kettle-protocol.md` doesn't define the checksum algorithm, the status frame layout, or the on/off `value` encoding. Those need capture evidence before any code is written.
5. **Validation:** a compile or host test can't prove BLE or appliance behavior. Only physical tests on your kettle can, and those need your explicit authorization.

## Unknowns

- Whether you have, or will buy, a companion MCU.
- Whether a computer counts as acceptable instead of a phone.
- The checksum algorithm, the status frame format, and the on/off value encoding.
- The toolchain and SDK state on your machine.

Tell me which route you prefer. I can then write the Flipper FAP plus the UART protocol spec, and either the ESP32 firmware or a host script.
