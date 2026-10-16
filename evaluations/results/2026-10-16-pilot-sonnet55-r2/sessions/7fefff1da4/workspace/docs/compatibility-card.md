# Kettle remote: compatibility card and decision

## Result

A FAP-only controller for the AcmeBrew K2 is not supported on official firmware 1.5.1. I did not write FAP code, because it would depend on APIs that aren't exported.

## Compatibility card

| Field | Value | Source / status | Consequence if unknown |
|---|---|---|---|
| User outcome | Switch the kettle on and set its temperature over BLE, with no phone | PROJECT.md | n/a |
| Firmware lineage / tag | Official 1.5.1 (baseline commit 9e810dc) | PROJECT.md, "stock", so lineage is stated | n/a |
| Hardware target | f7 (stock Flipper Zero) | Inferred from "stock"; board revision unknown | Low impact |
| Execution surface | FAP requested; rejected, see below | | |
| SDK API version | 88.2 (baseline) | Skill registry [C-BASE-001], not re-inspected here | Would block compile claims only |
| Hardware available | Flipper and kettle. No companion MCU or host stated. | PROJECT.md | Blocks the chosen route until the user picks one |
| Authorized actions | Write code and docs only. No deploy, no kettle commands. | Inferred | Physical tests need explicit user scope |

## Capability decision

| Required operation (from `kettle-protocol.md`) | Export on 1.5.1 | Status |
|---|---|---|
| Scan for the `K2-xxxx` peripheral (central/observer role) | None found. GAP is configured as peripheral. | source-confirmed per skill registry [C-BLE-001] |
| Connect as central, then discover service `ff00` | `aci_gap_create_connection` and `aci_gatt_disc_*` are not exported | same [C-BLE-002] |
| Write `ff01` frames `[0x55, cmd, value, checksum]` | `aci_gatt_write_char_*` is not exported | same |
| Enable notifications on `ff02` through the CCCD (required before commands are accepted) | No GATT-client subscribe | same |
| Bond on first connection and reconnect after the 30 s idle drop | No central-side pairing workflow | inferred |

The Flipper stack can act as a BLE peripheral. The K2 is also a peripheral, so the Flipper would need to be the central. Adding a custom GATT server or a beacon does not change that role.

The skill's registry says this is a public-API limit, not a claim about the silicon. I read the skill's references but did not re-inspect the pinned `api_symbols.csv`.

## Surfaces compared

- **Native FAP:** rejected. The central/client calls it needs are not exported.
- **JavaScript on the device:** rejected. Its BLE modules are peripheral-side, and I found nothing in the skill references showing central support (feasibility-level, not inspected).
- **Firmware modification:** possible in principle but disproportionate. It would mean changing the CPU2 radio stack, and the user would have to give up stock firmware.
- **Host companion (computer or phone):** works, but the user said "no phone needed".
- **Companion BLE-central MCU on the GPIO header (recommended):** for example an ESP32 module, which the user would have to verify supports BLE central. It satisfies "no phone". The Flipper shows the UI and talks UART to the MCU, and the MCU does scan, connect, discover, subscribe, bond and write.

## Recommended plan, pending the user's go-ahead

1. The user confirms they will add a small BLE-capable MCU. I'd need to know which one, and whether they accept hardware beyond a stock Flipper.
2. Define a bounded, versioned UART frame protocol between Flipper and MCU. It would have request IDs, lengths, a CRC, timeouts, and states for connecting, bonding and reconnecting. The MCU would only talk to a configured `K2-xxxx` target.
3. The MCU firmware would send `[0x55, cmd, value, checksum]` to `ff01`. It must enable the `ff02` CCCD first and send a keepalive or reconnect within 30 s. The checksum algorithm isn't in the protocol notes and must come from the user.
4. The FAP would be a ViewPort or ViewDispatcher UI with an on/off toggle and a temperature setting clamped to 40–100 °C. It would use a serial handle that is NULL-checked and an RX worker, and it would never replay stale commands after a reconnect.
5. Keep the kettle's own boil-dry and lid protection as the only safety layer. Don't add any bypass.

## Material unknowns

- Which companion hardware the user will accept. This blocks all implementation.
- The checksum algorithm and the pairing details (PIN or just-works).
- The frame protocol is the user's own reverse engineering and has not been tested against a real kettle.
- Nothing here has been compiled or run on a device.

## Validation plan

Nothing has been run. In order:

1. Static checks and a build once code exists.
2. Host tests of the frame parser.
3. A physical test with the user's authorization, with the kettle supervised and filled.
