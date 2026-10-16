/* Host test for soil_proto. Build and run (not executed in the authoring session):
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I.. test_soil_proto.c ../soil_proto.c -o /tmp/t && /tmp/t
 * Fixtures mirror captures/*.txt. CRC bytes there are assumed little endian; if the first
 * fixture check fails with a CRC mismatch, verify the byte order against the real board.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "soil_proto.h"

static SoilEvents feed_all(SoilParser* p, const uint8_t* d, size_t n) {
    SoilEvents ev = {0};
    for(size_t i = 0; i < n; i++) soil_parser_feed(p, d[i], &ev);
    return ev;
}

static size_t build(uint8_t* out, const uint8_t* payload, uint8_t n) {
    out[0] = SOIL_SYNC_BYTE;
    out[1] = n;
    memcpy(&out[2], payload, n);
    const uint16_t crc = soil_crc16(&out[1], (size_t)n + 1);
    out[2 + n] = (uint8_t)(crc & 0xFF);
    out[3 + n] = (uint8_t)(crc >> 8);
    return (size_t)n + 4;
}

int main(void) {
    /* CRC-16/CCITT-FALSE check value */
    assert(soil_crc16((const uint8_t*)"123456789", 9) == 0x29B1);

    SoilParser p;
    soil_parser_reset(&p);

    /* Capture: burst of three frames then a frame split across reads */
    const uint8_t burst[] = {0xA5, 0x04, 0x2C, 0x01, 0xE1, 0x00, 0xC5, 0xF4, 0xA5, 0x04, 0x2D, 0x01,
                             0xE2, 0x00, 0x22, 0xD7, 0xA5, 0x04, 0x2E, 0x01, 0xE3, 0x00, 0xCF, 0x7F};
    SoilEvents ev = feed_all(&p, burst, sizeof(burst));
    assert(ev.frames_ok == 3 && ev.errors == 0 && ev.has_reading);
    assert(ev.reading.moisture == 0x012E && ev.reading.temperature_dc == 0x00E3);

    const uint8_t part1[] = {0xA5, 0x04, 0x2F, 0x01};
    const uint8_t part2[] = {0xE4, 0x00, 0xEC, 0x90};
    ev = feed_all(&p, part1, sizeof(part1));
    assert(ev.frames_ok == 0 && !ev.has_reading);
    ev = feed_all(&p, part2, sizeof(part2));
    assert(ev.frames_ok == 1 && ev.reading.moisture == 0x012F);

    /* Capture: noise, over-long length, bad CRC, bad payload CRC, then a valid frame */
    soil_parser_reset(&p);
    const uint8_t noise[] = {0xFF, 0x00, 0x13, 0xA5, 0xC8, 0x01, 0x02, 0x03, 0xA5, 0x04, 0x2C, 0x01,
                             0xE1, 0x00, 0xC5, 0x0B, 0xA5, 0x04, 0x2C, 0x01, 0xF1, 0xFF, 0x46, 0xE9};
    ev = feed_all(&p, noise, sizeof(noise));
    assert(ev.errors >= 2); /* over-long length + at least one bad-CRC frame */
    assert(p.count == 0 || p.buf[0] == SOIL_SYNC_BYTE);

    /* Synthetic: valid frame hidden inside a corrupt one must be recovered */
    soil_parser_reset(&p);
    uint8_t good[SOIL_MAX_FRAME], stream[128];
    const uint8_t pl[4] = {0x10, 0x01, 0x9C, 0xFF}; /* moisture 272, temp -10.0 */
    size_t gl = build(good, pl, 4);
    size_t n = 0;
    stream[n++] = 0xA5; /* false sync */
    stream[n++] = 0x01;
    memcpy(&stream[n], good, gl); /* false frame: len 1, payload A5, crc = 04 10 -> rejected */
    n += gl;
    ev = feed_all(&p, stream, n);
    assert(ev.frames_ok == 1 && ev.errors == 1);
    assert(ev.reading.moisture == 272 && ev.reading.temperature_dc == -100);

    /* Range and short payload are rejected */
    soil_parser_reset(&p);
    const uint8_t hi[4] = {0xE9, 0x03, 0x00, 0x00}; /* 1001 */
    n = build(stream, hi, 4);
    ev = feed_all(&p, stream, n);
    assert(ev.frames_ok == 0 && ev.errors == 1 && !ev.has_reading);
    const uint8_t sh[2] = {0x01, 0x00};
    n = build(stream, sh, 2);
    ev = feed_all(&p, stream, n);
    assert(ev.frames_ok == 0 && ev.errors == 1);

    /* Max-length payload with reserved bytes is accepted */
    uint8_t big[SOIL_MAX_PAYLOAD] = {0xF4, 0x01, 0x05, 0x00};
    n = build(stream, big, SOIL_MAX_PAYLOAD);
    ev = feed_all(&p, stream, n);
    assert(ev.frames_ok == 1 && ev.reading.moisture == 500);

    /* Timeout discards a partial frame exactly once */
    ev = (SoilEvents){0};
    soil_parser_feed(&p, 0xA5, &ev);
    soil_parser_timeout(&p, &ev);
    soil_parser_timeout(&p, &ev);
    assert(ev.errors == 1 && p.count == 0);

    puts("all soil_proto tests passed");
    return 0;
}
