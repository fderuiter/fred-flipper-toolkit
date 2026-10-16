# Hardware and communications

Load for GPIO, UART, buses, ADC/PWM, USB, radios or companion devices. Hardware actions (driving pins, transmitting, writing tags, HID, appliance commands) need explicit user scope; this file guides design and review only.

## General rules

- Peripherals are shared with firmware services. Acquire through the highest supported API, check the result, release on every exit path [C-BUS-001].
- An exported init symbol is not permission to reinitialize global hardware.
- Header I/O is 3.3 V logic behind 51 Ω series resistors; 20 mA absolute maximum per pin and 100 mA total. The MCU pins are FT-family, but 5 V tolerance needs the MCU powered, pulls disabled and no analog function, and the firmware leaves header pins in analog mode by default; one official page says "5 V tolerant", another "3.3 V". Treat every header pin as 3.3 V only and level-shift 5 V modules [C-GPIO-001].
- Verify pin, function, mux and current owner in the pinned `furi_hal_resources.c` before giving wiring advice.
- Restore only state this app changed; do not "restore" global state another owner set.

## UART pattern (bounded, IRQ-safe)

Facts at 9e810dc: `furi_hal_serial_control_acquire` returns NULL if the interface is in use (logging or expansion may own it) [C-UART-001]; async and DMA RX callbacks run in interrupt context [C-UART-001]; IRQ-side queue/stream operations need zero timeout [C-CTX-001].

Design:

1. Acquire handle → NULL means busy: report it, do not retry forever.
2. `furi_hal_serial_init(handle, baud)`; start async RX with a callback that only reads available bytes into a `FuriStreamBuffer` (zero timeout) and counts overflow when the send is short.
3. A worker thread receives from the stream with a timeout, runs a framing state machine (max frame length, resync on bad length/CRC, inter-byte timeout), and posts parsed events to the app with a full-queue policy.
4. Cancel: set stop flag, wake worker, then `furi_hal_serial_async_rx_stop` → `furi_hal_serial_deinit` → `furi_hal_serial_control_release`, join the worker, free the stream buffer last (after no producer can touch it).
5. Report counters (overflow, framing errors, resyncs) instead of silently dropping.

Physical tests (bursts at line rate, disconnect, noise, busy handle) remain pending until authorized hardware testing.

## Buses and analog

- I²C: `furi_hal_i2c_acquire/release` with timeouts; handle absent device, NACK and stuck bus; check address encoding in the implementation; external bus only [C-BUS-001].
- SPI: acquire/release external handle; chip-select lifetime; buffer lifetime across DMA; timeouts [C-BUS-001].
- ADC: `furi_hal_adc_acquire`, configure, read, release; input range and reference. PWM: start/set/stop on supported outputs; define default and stop state [C-BUS-001].

## USB

The Flipper is a USB device, not a general USB host. Changing USB configuration can disconnect qFlipper/CLI/RPC; save and restore the previous configuration you replaced [C-USB-001].

## BLE

Baseline GAP is peripheral; exported `ble_gatt_*` calls are server-side (service/characteristic add/update/delete). No central/GATT-client workflow is exported [C-BLE-001] [C-BLE-002]. Use the stress test in [capability-map.md](capability-map.md#ble-appliance-stress-test).

## Radios, NFC, RFID, IR, iButton

Support is technology- and role-specific. Identify the protocol, role (reader/listener/emulator/transmitter), worker lifecycle, buffer lifetimes and stop behavior from pinned source and exports before designing. Region rules and authorization for transmission are the user's responsibility; FRED does not transmit or write tags.

## Companion MCU / host

A companion's capabilities are not the Flipper's. Specify a versioned framed protocol (request ID, length, CRC, timeout/retry, reconnect state, allowlist), power budget and logic levels. CRC detects corruption; it is not authentication. Test the protocol parser offline first.
