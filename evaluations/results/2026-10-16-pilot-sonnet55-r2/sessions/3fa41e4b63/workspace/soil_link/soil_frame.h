#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Pure framing logic: no Furi dependencies, so it can be host-tested.
// Frame: A5 | N (0-32) | payload[N] | CRC16 (LE, CCITT-FALSE over N+payload)
// Payload: u16 moisture (0-1000), i16 temperature (0.1 C), rest reserved.

#define SOIL_SYNC_BYTE   0xA5
#define SOIL_MAX_PAYLOAD 32
#define SOIL_MIN_PAYLOAD 4
#define SOIL_MAX_MOISTURE 1000
#define SOIL_FRAME_MAX   (2 + SOIL_MAX_PAYLOAD + 2)

typedef struct {
    uint16_t moisture; // 0..1000
    int16_t temperature_dc; // 0.1 C units
} SoilReading;

typedef struct {
    uint8_t buf[SOIL_FRAME_MAX];
    uint8_t fill;
    uint32_t frames_ok;
    uint32_t errors; // bad length, bad CRC, short payload, out-of-range moisture
    uint32_t skipped_bytes; // bytes discarded while hunting for sync
} SoilParser;

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t len);

void soil_parser_reset(SoilParser* p);

// Feed one byte. Returns true and fills *out when a valid frame completed.
bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out);
