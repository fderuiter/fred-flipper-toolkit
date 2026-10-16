// Host test: gcc -I.. -o t test_frame.c ../soil_frame.c && ./t
#include "soil_frame.h"
#include <stdio.h>

static int fails;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

static int feed(SoilParser* p, const uint8_t* d, size_t n, SoilReading* last) {
    int frames = 0;
    for(size_t i = 0; i < n; i++) if(soil_parser_feed(p, d[i], last)) frames++;
    return frames;
}

int main(void) {
    SoilParser p; SoilReading r = {0};

    // burst: three frames, fourth split across two reads
    const uint8_t burst1[] = {0xA5,0x04,0x2C,0x01,0xE1,0x00,0xC5,0xF4, 0xA5,0x04,0x2D,0x01,0xE2,0x00,0x22,0xD7,
                              0xA5,0x04,0x2E,0x01,0xE3,0x00,0xCF,0x7F, 0xA5,0x04,0x2F,0x01};
    const uint8_t burst2[] = {0xE4,0x00,0xEC,0x90};
    soil_parser_reset(&p);
    CHECK(feed(&p, burst1, sizeof(burst1), &r) == 3);
    CHECK(feed(&p, burst2, sizeof(burst2), &r) == 1);
    CHECK(r.moisture == 0x012F && r.temperature_dc == 0x00E4);
    CHECK(p.frame_count == 4 && p.error_count == 0);

    // noise: garbage, over-long length, bad CRC, then valid frame
    const uint8_t noise[] = {0xFF,0x00,0x13,0xA5,0xC8,0x01,0x02,0x03,0xA5,0x04,0x2C,0x01,0xE1,0x00,0xC5,0x0B,
                             0xA5,0x04,0x2C,0x01,0xF1,0xFF,0x46,0xE9};
    soil_parser_reset(&p);
    int n = feed(&p, noise, sizeof(noise), &r);
    printf("noise: frames=%d errors=%u moisture=%u temp=%d\n", n, (unsigned)p.error_count, r.moisture, r.temperature_dc);
    CHECK(n == 1);
    CHECK(r.moisture == 0x012C && r.temperature_dc == -15);
    CHECK(p.error_count >= 3);

    // overlapping: a corrupt header whose payload hides a valid frame
    const uint8_t hidden[] = {0xA5,0x08, 0xA5,0x04,0x2C,0x01,0xE1,0x00,0xC5,0xF4};
    soil_parser_reset(&p);
    CHECK(feed(&p, hidden, sizeof(hidden), &r) == 1);

    // zero-length frame, CRC valid but payload too short -> error, not reading
    uint8_t z[4] = {0xA5, 0x00, 0, 0};
    uint16_t c = soil_crc16(z + 1, 1); z[2] = c & 0xFF; z[3] = c >> 8;
    soil_parser_reset(&p);
    CHECK(feed(&p, z, 4, &r) == 0 && p.error_count == 1);

    // long stream of garbage never overflows / crashes
    soil_parser_reset(&p);
    for(int i = 0; i < 100000; i++) soil_parser_feed(&p, (uint8_t)(i % 3 ? 0xA5 : 0x20), &r);
    CHECK(p.len < SOIL_MAX_FRAME_SIZE);

    printf(fails ? "FAILED\n" : "ALL PASS\n");
    return fails != 0;
}
