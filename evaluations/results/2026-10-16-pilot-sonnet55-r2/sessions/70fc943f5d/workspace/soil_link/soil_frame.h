#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOIL_SYNC        0xA5
#define SOIL_MAX_PAYLOAD 32

typedef struct {
    uint16_t moisture; // 0..1000
    int16_t temperature_dc; // 0.1 degC units
} SoilReading;

typedef enum {
    SoilStateSync,
    SoilStateLen,
    SoilStatePayload,
    SoilStateCrc0,
    SoilStateCrc1,
} SoilState;

typedef struct {
    SoilState state;
    uint8_t len;
    uint8_t pos;
    uint8_t buf[1 + SOIL_MAX_PAYLOAD]; // length byte + payload
    uint8_t crc_lo;
    uint32_t error_count;
    uint32_t ok_count;
} SoilParser;

void soil_parser_reset(SoilParser* p);

/** Feed one byte. Returns true when a valid frame completed; reading is filled in. */
bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* reading);

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t len);
