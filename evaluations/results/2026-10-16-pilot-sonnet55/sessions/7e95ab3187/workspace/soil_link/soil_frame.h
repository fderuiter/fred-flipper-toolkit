#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Pure framing logic: no Furi calls, host-testable.

#define SOIL_SYNC        0xA5
#define SOIL_MAX_PAYLOAD 32
#define SOIL_MIN_PAYLOAD 4
#define SOIL_MAX_FRAME   (2 + SOIL_MAX_PAYLOAD + 2)

typedef struct {
    uint16_t moisture; // 0..1000
    int16_t temperature_dc; // 0.1 degC
} SoilReading;

typedef struct {
    uint8_t buf[SOIL_MAX_FRAME];
    uint8_t len;
    uint32_t error_count; // bad CRC, bad length, short payload, out-of-range, garbage runs
    uint32_t frame_count; // valid frames
    bool in_garbage; // true while skipping non-sync bytes (counts one error per run)
} SoilParser;

void soil_parser_reset(SoilParser* p);

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout).
uint16_t soil_crc16(const uint8_t* data, size_t len);

// Feed one byte. Returns true and fills *out when a valid frame completed.
// At most one frame completes per byte.
bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out);
