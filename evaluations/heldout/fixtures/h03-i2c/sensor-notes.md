# HX-40 humidity/temperature sensor (synthetic)

- I2C, 7-bit address `0x44`, standard mode (100 kHz) and fast mode (400 kHz).
- Measurement: write the single command byte `0xF5`, wait at least 15 ms, then read 6 bytes:
  `RH_MSB RH_LSB RH_CRC T_MSB T_LSB T_CRC`.
- CRC-8 over each 2-byte word: polynomial `0x31`, initial value `0xFF`, no reflection, no final XOR (word `BE EF` → `0x92`).
- Conversion: `RH % = 100 × raw / 65535`, `T °C = −45 + 175 × raw / 65535`.
- The sensor NACKs its address while a measurement is still running.
- After a brownout the sensor can hold SDA low. Datasheet recovery: with SDA released, clock SCL up to 9 times until SDA reads high, then generate a STOP condition. The pins are open-drain; never drive either line high.
