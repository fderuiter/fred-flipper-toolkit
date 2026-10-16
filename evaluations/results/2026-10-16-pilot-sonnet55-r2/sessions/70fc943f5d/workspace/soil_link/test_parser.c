// Host test: gcc -Wall -o test_parser test_parser.c soil_frame.c && ./test_parser
#include "soil_frame.h"

#include <stdio.h>

static int feed_all(SoilParser* p, const uint8_t* d, size_t n, SoilReading* last) {
    int frames = 0;
    for(size_t i = 0; i < n; i++) {
        SoilReading r;
        if(soil_parser_feed(p, d[i], &r)) {
            *last = r;
            frames++;
        }
    }
    return frames;
}

int main(void) {
    int fail = 0;
    SoilParser p;
    SoilReading last = {0};

    // burst: three frames, then a fourth split across two reads
    const uint8_t b1[] = {0xA5, 4, 0x2C, 1, 0xE1, 0, 0xC5, 0xF4, 0xA5, 4, 0x2D, 1, 0xE2, 0,
                          0x22, 0xD7, 0xA5, 4, 0x2E, 1, 0xE3, 0, 0xCF, 0x7F, 0xA5, 4, 0x2F, 1};
    const uint8_t b2[] = {0xE4, 0, 0xEC, 0x90};
    soil_parser_reset(&p);
    int n = feed_all(&p, b1, sizeof(b1), &last);
    n += feed_all(&p, b2, sizeof(b2), &last);
    printf("burst: frames=%d errors=%u last=%u/%d\n", n, (unsigned)p.error_count, last.moisture,
           last.temperature_dc);
    if(n != 4 || p.error_count != 0 || last.moisture != 0x12F || last.temperature_dc != 0xE4) fail = 1;

    // noise: garbage, over-long length, bad CRC, then valid frame
    const uint8_t nz[] = {0xFF, 0, 0x13, 0xA5, 0xC8, 1, 2, 3, 0xA5, 4, 0x2C, 1, 0xE1, 0,
                          0xC5, 0x0B, 0xA5, 4, 0x2C, 1, 0xF1, 0xFF, 0x46, 0xE9};
    soil_parser_reset(&p);
    n = feed_all(&p, nz, sizeof(nz), &last);
    printf("noise: frames=%d errors=%u last=%u/%d\n", n, (unsigned)p.error_count, last.moisture,
           last.temperature_dc);
    if(n != 1 || p.error_count != 2) fail = 1;

    printf(fail ? "FAIL\n" : "PASS\n");
    return fail;
}
