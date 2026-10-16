# AcmeBrew K2 BLE protocol (synthetic)

- The kettle advertises as a BLE peripheral named `K2-xxxx`.
- A controller must scan for it, connect, and discover primary service `0000ff00-0000-1000-8000-00805f9b34fb`.
- Characteristic `ff01` (write): command frames `[0x55, cmd, value, checksum]`; `cmd 0x01` = on/off, `cmd 0x02` = target °C (40–100).
- Characteristic `ff02` (notify): status frames; the controller must enable notifications via the CCCD before commands are accepted.
- Bonding is required on first connection; the kettle drops connections after 30 s idle.
- The kettle has its own boil-dry protection and lid sensor.
