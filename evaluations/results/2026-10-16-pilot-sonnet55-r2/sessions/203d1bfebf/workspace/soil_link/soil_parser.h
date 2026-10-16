#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SOIL_SYNC        0xA5
#define SOIL_MAX_PAYLOAD 32
#define SOIL_MIN_PAYLOAD 4 // u16 moisture + i16 temperature
#define SOIL_MAX_FRAME   (2 + SOIL_MAX_PAYLOAD + 2)

typedef struct {
    uint8_t buf[SOIL_MAX_FRAME];
    size_t len;
    bool in_garbage; // true while skipping non-sync bytes, so a run counts once
    bool have_reading;
    uint16_t moisture; // 0..1000
    int16_t temperature_dc; // 0.1 degC
    uint32_t frames_ok;
    uint32_t errors;
} SoilParser;

void soil_parser_reset(SoilParser* p);

// Feed received bytes. Returns the number of valid frames decoded by this call.
size_t soil_parser_feed(SoilParser* p, const uint8_t* data, size_t size);

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t size);
