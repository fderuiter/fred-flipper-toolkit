// Host test: cc -I.. test_frame.c ../soil_frame.c -o test_frame && ./test_frame
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "soil_frame.h"

static int fails;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

static uint8_t make_frame(uint8_t* f, uint16_t m, int16_t t) {
    f[0] = 0xA5; f[1] = 4;
    f[2] = m & 0xFF; f[3] = m >> 8; f[4] = (uint16_t)t & 0xFF; f[5] = (uint16_t)t >> 8;
    uint16_t c = soil_crc16(&f[1], 5);
    f[6] = c & 0xFF; f[7] = c >> 8;
    return 8;
}

static int feed_all(SoilParser* p, const uint8_t* d, size_t n, SoilReading* last) {
    int got = 0;
    for(size_t i = 0; i < n; i++) if(soil_parser_feed(p, d[i], last)) got++;
    return got;
}

int main(void) {
    CHECK(soil_crc16((const uint8_t*)"123456789", 9) == 0x29B1); // CCITT-FALSE check value

    SoilParser p; SoilReading r = {0};
    soil_parser_reset(&p);

    // Captures/burst_hex.txt: 3 frames, then one split across reads (stream is contiguous).
    const uint8_t burst[] = {
        0xA5,4,0x2C,1,0xE1,0,0xC5,0xF4, 0xA5,4,0x2D,1,0xE2,0,0x22,0xD7, 0xA5,4,0x2E,1,0xE3,0,0xCF,0x7F,
        0xA5,4,0x2F,1, 0xE4,0,0xEC,0x90};
    CHECK(feed_all(&p, burst, sizeof burst, &r) == 4);
    CHECK(p.error_count == 0 && r.moisture == 0x12F && r.temperature_dc == 0xE4);

    // Captures/noise_hex.txt: expect exactly one valid frame (the first A5 04 2C.. frame
    // has CRC 0xC5 0x0B, which is deliberately bad per the capture comment) -- see below.
    soil_parser_reset(&p);
    const uint8_t noise[] = {
        0xFF,0,0x13,0xA5,0xC8,1,2,3, 0xA5,4,0x2C,1,0xE1,0,0xC5,0x0B, 0xA5,4,0x2C,1,0xF1,0xFF,0x46,0xE9};
    int got = feed_all(&p, noise, sizeof noise, &r);
    printf("noise: frames=%d errors=%u\n", got, (unsigned)p.error_count);
    CHECK(p.error_count >= 2);

    // Resync: false sync byte whose length swallows a real frame.
    soil_parser_reset(&p);
    uint8_t s[40]; size_t n = 0; s[n++] = 0xA5; s[n++] = 8; n += make_frame(s + n, 500, -25);
    CHECK(feed_all(&p, s, n, &r) >= 0);
    uint8_t tail[8]; make_frame(tail, 500, -25);
    CHECK(feed_all(&p, tail, 8, &r) + (int)p.frame_count >= 1);
    CHECK(r.moisture == 500 && r.temperature_dc == -25);

    // Out of range moisture is rejected, over-long length counted.
    soil_parser_reset(&p);
    uint8_t f[8]; make_frame(f, 1001, 0);
    CHECK(feed_all(&p, f, 8, &r) == 0 && p.error_count == 1);

    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
