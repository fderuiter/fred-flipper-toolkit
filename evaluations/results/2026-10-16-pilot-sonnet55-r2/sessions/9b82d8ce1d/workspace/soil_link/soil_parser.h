#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOIL_SYNC       0xA5
#define SOIL_MAX_PAYLOAD 32
#define SOIL_MIN_PAYLOAD 4 // u16 moisture + i16 temperature

typedef struct {
    uint16_t moisture; // 0..1000
    int16_t temperature_dc; // 0.1 degC
} SoilReading;

typedef struct {
    uint8_t state;
    uint8_t len;
    uint8_t pos;
    uint8_t buf[1 + SOIL_MAX_PAYLOAD + 2]; // length + payload + crc
} SoilParser;

typedef enum {
    SoilParseNone, // need more bytes
    SoilParseFrame, // a valid frame was decoded into *out
    SoilParseError, // a frame was rejected (CRC, range, short payload)
} SoilParseResult;

void soil_parser_reset(SoilParser* p);

/** Feed one byte. Never reads/writes outside the parser; always resynchronizes. */
SoilParseResult soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out);

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
