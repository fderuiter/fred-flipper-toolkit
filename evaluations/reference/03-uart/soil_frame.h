// Reviewer-side reference solution for FRED task 03. MIT licensed, original.
// Streaming parser for the synthetic soil-sensor frame protocol. Pure C99.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define SOIL_SYNC         0xA5u
#define SOIL_MAX_PAYLOAD  32u
#define SOIL_MOISTURE_MAX 1000u

typedef struct {
    uint16_t moisture; /* 0..1000 */
    int16_t temp_decicelsius;
} SoilReading;

typedef struct {
    uint32_t frames_ok;
    uint32_t bad_length;
    uint32_t bad_crc;
    uint32_t bad_payload;
    uint32_t resyncs;
} SoilStats;

typedef struct {
    uint8_t buf[2 + SOIL_MAX_PAYLOAD + 2]; /* len, payload, crc lo, crc hi (sync not stored) */
    size_t have;
    bool in_frame;
    SoilStats stats;
} SoilParser;

typedef void (*SoilReadingCallback)(const SoilReading* reading, void* context);

void soil_parser_reset(SoilParser* p);
/* Feed any number of bytes; calls cb for each valid reading. Bounded work:
 * at most O(n * frame length) per call, no allocation. */
void soil_parser_feed(
    SoilParser* p,
    const uint8_t* data,
    size_t n,
    SoilReadingCallback cb,
    void* ctx);
uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t n);
