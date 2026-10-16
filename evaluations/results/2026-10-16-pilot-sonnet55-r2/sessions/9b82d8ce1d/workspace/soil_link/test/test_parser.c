// Host test: gcc -Wall -Wextra -I.. test_parser.c ../soil_parser.c -o /tmp/test_parser && /tmp/test_parser
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "soil_parser.h"

static int fails;
#define CHECK(c) \
    do { \
        if(!(c)) { \
            printf("FAIL line %d: %s\n", __LINE__, #c); \
            fails++; \
        } \
    } while(0)

typedef struct {
    int frames, errors;
    SoilReading last;
} Tally;

static void feed(SoilParser* p, Tally* t, const uint8_t* d, size_t n) {
    for(size_t i = 0; i < n; i++) {
        SoilReading r;
        SoilParseResult res = soil_parser_feed(p, d[i], &r);
        if(res == SoilParseFrame) {
            t->frames++;
            t->last = r;
        } else if(res == SoilParseError) {
            t->errors++;
        }
    }
}

int main(void) {
    // CRC check value for CRC-16/CCITT-FALSE
    CHECK(soil_crc16_ccitt_false((const uint8_t*)"123456789", 9) == 0x29B1);

    // burst: 3 frames, then one frame split across two reads
    const uint8_t burst1[] = {0xA5, 4, 0x2C, 1, 0xE1, 0, 0xC5, 0xF4, 0xA5, 4, 0x2D, 1, 0xE2, 0,
                              0x22, 0xD7, 0xA5, 4, 0x2E, 1, 0xE3, 0, 0xCF, 0x7F, 0xA5, 4, 0x2F, 1};
    const uint8_t burst2[] = {0xE4, 0, 0xEC, 0x90};
    SoilParser p;
    Tally t = {0};
    soil_parser_reset(&p);
    feed(&p, &t, burst1, sizeof(burst1));
    CHECK(t.frames == 3 && t.errors == 0);
    feed(&p, &t, burst2, sizeof(burst2));
    CHECK(t.frames == 4 && t.errors == 0);
    CHECK(t.last.moisture == 0x012F && t.last.temperature_dc == 0x00E4);

    // noise: garbage, over-long length, bad CRC, then a valid frame
    const uint8_t noise[] = {0xFF, 0, 0x13, 0xA5, 0xC8, 1,    2,    3,    0xA5, 4,    0x2C, 1,
                             0xE1, 0, 0xC5, 0x0B, 0xA5, 4,    0x2C, 1,    0xF1, 0xFF, 0x46, 0xE9};
    memset(&t, 0, sizeof t);
    soil_parser_reset(&p);
    feed(&p, &t, noise, sizeof(noise));
    printf("noise: frames=%d errors=%d moisture=%u temp=%d\n", t.frames, t.errors,
           t.last.moisture, t.last.temperature_dc);
    CHECK(t.errors >= 2); // bad length + bad CRC
    CHECK(t.frames <= 1);

    // frame hidden inside a rejected one must be found (false sync followed by real frame)
    const uint8_t hidden[] = {0xA5, 0x08, 0xA5, 4, 0x2C, 1, 0xE1, 0, 0xC5, 0xF4};
    memset(&t, 0, sizeof t);
    soil_parser_reset(&p);
    feed(&p, &t, hidden, sizeof(hidden));
    CHECK(t.frames == 1);

    // fuzz: no crash / out-of-bounds (run with -fsanitize=address,undefined)
    srand(1);
    memset(&t, 0, sizeof t);
    soil_parser_reset(&p);
    for(int i = 0; i < 2000000; i++) {
        uint8_t b = (rand() % 3 == 0) ? 0xA5 : (uint8_t)(rand() % 40);
        feed(&p, &t, &b, 1);
    }

    printf(fails ? "FAILED\n" : "OK\n");
    return fails != 0;
}
