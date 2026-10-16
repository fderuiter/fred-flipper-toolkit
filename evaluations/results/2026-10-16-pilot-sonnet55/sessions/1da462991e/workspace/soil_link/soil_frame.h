#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SOIL_SYNC        0xA5
#define SOIL_MAX_PAYLOAD 32
#define SOIL_MIN_PAYLOAD 4
#define SOIL_MAX_FRAME   (2 + SOIL_MAX_PAYLOAD + 2)

typedef struct {
    uint16_t moisture; // 0..1000
    int16_t temperature_dc; // 0.1 degC units
} SoilReading;

typedef struct {
    uint8_t buf[SOIL_MAX_FRAME];
    size_t len;
    bool resyncing; // an error was already counted for the current bad stretch
    uint32_t error_count;
    uint32_t frame_count;
} SoilParser;

void soil_parser_reset(SoilParser* p);

/** Feed one byte. Returns true and fills *out when a valid frame completed. */
bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out);

/** CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout). */
uint16_t soil_crc16(const uint8_t* data, size_t len);
