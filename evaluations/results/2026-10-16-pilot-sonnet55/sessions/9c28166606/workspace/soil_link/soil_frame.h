#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Frame: A5 | N | payload[N] | crc16 LE (CRC-16/CCITT-FALSE over N and payload)
// Payload: u16 moisture (0..1000), i16 temperature (0.1 C), remaining bytes reserved.
#define SOIL_SYNC          0xA5
#define SOIL_MAX_PAYLOAD   32
#define SOIL_MIN_PAYLOAD   4
#define SOIL_MAX_FRAME     (2 + SOIL_MAX_PAYLOAD + 2)
#define SOIL_MOISTURE_MAX  1000

typedef struct {
    uint16_t moisture;
    int16_t temperature_dc; // tenths of a degree C
} SoilReading;

typedef struct {
    uint8_t buf[SOIL_MAX_FRAME];
    uint8_t len;
    bool in_garbage; // inside a run of non-sync bytes, already counted
    uint32_t error_count; // garbage runs, bad length, bad CRC, bad payload
    uint32_t frame_count; // valid frames
} SoilParser;

void soil_parser_reset(SoilParser* p);

uint16_t soil_crc16(const uint8_t* data, size_t len);

// Feed one byte. Returns true and fills *out when a valid frame completed.
// Bounded: never writes past buf, never loops more than the buffered bytes.
bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out);
