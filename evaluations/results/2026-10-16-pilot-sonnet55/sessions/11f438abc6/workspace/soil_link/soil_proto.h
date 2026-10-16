#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SOIL_SYNC        0xA5
#define SOIL_MAX_PAYLOAD 32
#define SOIL_MIN_PAYLOAD 4 // u16 moisture + i16 temperature
#define SOIL_BUF_SIZE    (2 + SOIL_MAX_PAYLOAD + 2)

typedef struct {
    uint8_t buf[SOIL_BUF_SIZE];
    size_t len;
    bool in_garbage; // inside a run of non-sync bytes (counted once)
    uint32_t errors;
    uint32_t frames;
    bool have_reading;
    uint16_t moisture; // 0..1000
    int16_t temp_dc; // 0.1 degC
} SoilParser;

void soil_parser_reset(SoilParser* p);
uint16_t soil_crc16(const uint8_t* data, size_t len);
/** Feed one byte; returns true if it completed a valid frame (reading updated). */
bool soil_parser_feed(SoilParser* p, uint8_t byte);
