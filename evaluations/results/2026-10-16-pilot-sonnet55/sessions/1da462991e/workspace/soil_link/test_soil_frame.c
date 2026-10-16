// Host test: gcc -Wall -o t test_soil_frame.c soil_frame.c && ./t
// Hex strings copied from ../captures/. Expected counts are derived by hand from the parser rules.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soil_frame.h"

static int feed_hex(SoilParser* p, const char* hex, SoilReading* last) {
    int frames = 0;
    for(const char* s = hex; *s;) {
        if(*s == ' ' || *s == '\n') {
            s++;
            continue;
        }
        uint8_t b = (uint8_t)strtoul((char[]){s[0], s[1], 0}, NULL, 16);
        s += 2;
        if(soil_parser_feed(p, b, last)) frames++;
    }
    return frames;
}

static int fails;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

int main(void) {
    SoilParser p;
    SoilReading r = {0};

    // Burst: 3 frames, then one frame split across two reads
    soil_parser_reset(&p);
    int n = feed_hex(&p, "A5 04 2C 01 E1 00 C5 F4 A5 04 2D 01 E2 00 22 D7 A5 04 2E 01 E3 00 CF 7F", &r);
    n += feed_hex(&p, "A5 04 2F 01", &r);
    n += feed_hex(&p, "E4 00 EC 90", &r);
    CHECK(n == 4);
    CHECK(p.error_count == 0);
    CHECK(r.moisture == 0x012F && r.temperature_dc == 0x00E4);

    // Noise: garbage, over-long length, bad CRC, then a valid frame
    soil_parser_reset(&p);
    n = feed_hex(&p, "FF 00 13 A5 C8 01 02 03 A5 04 2C 01 E1 00 C5 0B A5 04 2C 01 F1 FF 46 E9", &r);
    CHECK(n == 1);
    CHECK(r.moisture == 300 && r.temperature_dc == -15);
    CHECK(p.error_count == 3);

    printf(fails ? "FAILED\n" : "ok\n");
    return fails != 0;
}
