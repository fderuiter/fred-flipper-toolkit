#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOIL_SYNC        0xA5
#define SOIL_MAX_PAYLOAD 32
#define SOIL_MAX_FRAME   (2 + SOIL_MAX_PAYLOAD + 2)

typedef struct {
    uint8_t buf[SOIL_MAX_FRAME];
    size_t len;
    bool in_garbage; // inside a run of unusable bytes, already counted once

    bool has_reading;
    uint16_t moisture; // 0..1000
    int16_t temperature; // 0.1 degC
    uint32_t frames_ok;
    uint32_t errors;
} SoilParser;

void soil_parser_reset(SoilParser* p);

// Feed one received byte. Returns true if this byte completed a valid frame.
bool soil_parser_feed(SoilParser* p, uint8_t byte);

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t len);
