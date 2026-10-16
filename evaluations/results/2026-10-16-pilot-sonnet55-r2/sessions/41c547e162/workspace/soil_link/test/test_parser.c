// Host test: gcc -I.. test_parser.c ../soil_parser.c -o t && ./t
#include <assert.h>
#include <stdio.h>
#include "soil_parser.h"

static void feed(SoilParser* p, const uint8_t* d, size_t n) {
    for(size_t i = 0; i < n; i++) soil_parser_feed(p, d[i]);
}

int main(void) {
    SoilParser p;

    // burst: 3 frames back to back, 4th split across two reads
    static const uint8_t a[] = {0xA5,0x04,0x2C,0x01,0xE1,0x00,0xC5,0xF4,
                                0xA5,0x04,0x2D,0x01,0xE2,0x00,0x22,0xD7,
                                0xA5,0x04,0x2E,0x01,0xE3,0x00,0xCF,0x7F,
                                0xA5,0x04,0x2F,0x01};
    static const uint8_t b[] = {0xE4,0x00,0xEC,0x90};
    soil_parser_reset(&p);
    feed(&p, a, sizeof(a));
    assert(p.frames_ok == 3);
    feed(&p, b, sizeof(b));
    assert(p.frames_ok == 4 && p.errors == 0);
    assert(p.moisture == 0x012F && p.temperature == 0xE4);

    // noise: garbage, over-long length, bad CRC, then (CRC-bad) 0xFF46 temp frame
    static const uint8_t n[] = {0xFF,0x00,0x13,0xA5,0xC8,0x01,0x02,0x03,
                                0xA5,0x04,0x2C,0x01,0xE1,0x00,0xC5,0x0B,
                                0xA5,0x04,0x2C,0x01,0xF1,0xFF,0x46,0xE9};
    soil_parser_reset(&p);
    feed(&p, n, sizeof(n));
    printf("noise: ok=%u errors=%u\n", (unsigned)p.frames_ok, (unsigned)p.errors);
    puts("ok");
    return 0;
}
