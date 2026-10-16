#pragma once

/* Pure framing/parsing logic for the soil sensor link. No Furi dependencies so it can be
 * host-tested. Not thread-safe: one parser is owned by exactly one thread. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SOIL_SYNC_BYTE    0xA5
#define SOIL_MAX_PAYLOAD  32
#define SOIL_MIN_PAYLOAD  4 /* u16 moisture + i16 temperature */
#define SOIL_MAX_FRAME    (2 + SOIL_MAX_PAYLOAD + 2)
#define SOIL_MOISTURE_MAX 1000

typedef struct {
    uint16_t moisture; /* 0..1000 */
    int16_t temperature_dc; /* 0.1 degC units */
} SoilReading;

/* Result of feeding bytes. The caller zeroes it once and accumulates across calls, or reads and
 * clears it after each call. */
typedef struct {
    bool has_reading; /* true if `reading` holds a new valid reading (latest one wins) */
    SoilReading reading;
    uint32_t frames_ok; /* valid frames accepted */
    uint32_t errors; /* rejected frames: bad length, bad CRC, short payload, range, timeout */
} SoilEvents;

typedef struct {
    uint8_t buf[SOIL_MAX_FRAME];
    uint8_t count; /* bytes of the current candidate frame in buf; 0 = hunting for sync */
} SoilParser;

void soil_parser_reset(SoilParser* p);

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, xorout 0. */
uint16_t soil_crc16(const uint8_t* data, size_t len);

/* Feed one byte. Bytes outside a frame are skipped silently (not counted as errors). When a
 * candidate frame is rejected, the bytes after its sync byte are re-scanned so a real frame that
 * began inside the corrupt one is not lost; each rejected candidate counts one error. */
void soil_parser_feed(SoilParser* p, uint8_t byte, SoilEvents* ev);

/* Inter-byte timeout: discards a partially received frame and counts one error. No-op when idle. */
void soil_parser_timeout(SoilParser* p, SoilEvents* ev);
