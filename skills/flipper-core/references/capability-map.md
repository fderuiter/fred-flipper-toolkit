# Capability map

Use for feasibility and surface selection. "Public" means relevant exported rows were found in pinned `api_symbols.csv` [C-BASE-002]; it does not endorse every operation in a subsystem. A public capability needs header, implementation, export or declared library, resource owner and a build [C-FAP-003].

## Execution surfaces

| Surface | Use when | Boundaries | Typical mistake | FRED support |
|---|---|---|---|---|
| External native FAP | Standalone UI, public peripheral/protocol APIs, local files | Loaded into RAM; only exported symbols; shares hardware with firmware [C-FAP-001] | Assuming firmware internals or BLE central are available | researched implementation guidance |
| Built-in app | Feature intentionally shipped in a firmware image | Full FBT build, release responsibility | Treating built-in as privilege that removes limits | feasibility-only |
| Firmware service/modification | System hooks, new exports, stack changes | Exact firmware/radio stack, recovery/debug plan | Forking firmware for a simple UI feature | feasibility-only |
| On-device JavaScript | Small module-backed automation, quick iteration | Flipper's mJS-derived runtime and modules [C-JS-001] | Node/browser APIs, npm, assuming TypeScript adds runtime features | feasibility-only |
| Host companion | Networking, heavy parsing, BLE central on host, data analysis | Host OS permissions, serial/BLE/RPC protocol | Assuming host BLE capability lives on the Flipper | feasibility-only |
| Expansion microcontroller | BLE central, Wi-Fi, timing outside public Flipper surface | Separate firmware, electrical design, framed link | Assuming a devboard has every chip variant's features | feasibility-only |

## Decision procedure

1. Write down the required operations (roles, transports, timing, storage, UI) from the user's target, not from the product name.
2. For each operation trace header → implementation → export/library → resource owner on the baseline.
3. If any required operation is missing on the FAP surface, compare surfaces by that operation, not by convenience.
4. Record the chosen surface, rejected surfaces with the reason, unknowns and test plan in the [compatibility card](../assets/compatibility-card.md).
5. "Unsupported on this surface" with a working alternative is a complete, useful answer.

## Domains

| Domain | Public starting point | Internal / unsupported boundary |
|---|---|---|
| GPIO | `furi_hal_gpio_*`, resources | Pin/function/voltage constraints; no arbitration by register access [C-GPIO-001] |
| UART | `furi_hal_serial_control_acquire`, async/DMA RX | Handle may be NULL when busy; RX callbacks in IRQ [C-UART-001] |
| I²C / SPI | `furi_hal_i2c_acquire`, `furi_hal_spi_acquire` and transfers | Internal buses are not user buses; DMA not arbitrary [C-BUS-001] |
| ADC / PWM | `furi_hal_adc_acquire`, `furi_hal_pwm_start` | Channel/mux/timer constraints [C-BUS-001] |
| USB | USB config and supplied HID/CDC interfaces | Device role only, not a USB host; reconfiguring can drop CLI/RPC [C-USB-001] |
| BLE | BT record/profile lifecycle, `ble_gatt_*` server-side service/characteristic APIs | GAP configured as peripheral; no exported central/GATT-client workflow [C-BLE-001] [C-BLE-002] |
| Sub-GHz, NFC, LF RFID, Infrared, iButton | Protocol/worker libraries per technology | Technology/role-specific, not universal access; device tests required |
| Expansion/companion | External UART/SPI/I²C | Companion capabilities are not inherited by the Flipper |

## BLE appliance stress test

Most BLE appliances are peripherals/GATT servers. Controlling one requires scan, connect as central, service discovery, read/write and often subscribe, pairing and reconnect. At the baseline `gap_init` configures `GAP_PERIPHERAL_ROLE`, and no central/GATT-client calls (`aci_gap_create_connection`, `aci_gatt_disc_*`, `aci_gatt_read_char_*`, `aci_gatt_write_char_*`) are exported [C-BLE-001] [C-BLE-002]. A custom GATT server or beacon does not change the role.

**Result: an ordinary official-firmware FAP-only controller is not supported by the researched public API.** This is not a claim that the silicon cannot do it; a firmware/CPU2 change would need its own investigation.

Default recommendation: Flipper UI + a BLE-central-capable companion MCU over UART (verify the chip's central/client support and 3.3 V compatibility first), or a host companion when a computer/phone is acceptable. Define a bounded, versioned framed protocol (request IDs, lengths, CRC for corruption, timeouts, reconnect states, target allowlist). CRC is not authentication. Keep the appliance's safety controls and local stop behavior; never replay stale commands after reconnect. Appliance behavior stays pending until authorized physical tests.
