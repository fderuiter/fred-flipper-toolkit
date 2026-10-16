#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SOIL_SYNC_BYTE      0xA5
#define SOIL_MAX_PAYLOAD    32
#define SOIL_MAX_FRAME_SIZE (2 + SOIL_MAX_PAYLOAD + 2)
#define SOIL_MIN_PAYLOAD    4
#define SOIL_MOISTURE_MAX   1000

typedef struct {
    uint16_t moisture; // 0..1000
    int16_t temperature_dc; // 0.1 degC units
} SoilReading;

// Bounded resynchronising frame parser. Pure logic: no Furi calls.
typedef struct {
    uint8_t buf[SOIL_MAX_FRAME_SIZE];
    size_t len;
    bool in_garbage;
    uint32_t error_count;
    uint32_t frame_count;
} SoilParser;

void soil_parser_reset(SoilParser* p);

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout).
uint16_t soil_crc16(const uint8_t* data, size_t len);

// Feed one byte. Returns true and fills *out when a valid frame completed.
// A failed candidate frame is rescanned from its second byte, so a real frame
// overlapping a corrupt one is still found.
bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out);
