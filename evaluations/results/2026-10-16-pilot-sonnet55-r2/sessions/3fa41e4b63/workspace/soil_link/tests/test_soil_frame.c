// Host test: cc -Wall -Wextra -o /tmp/t soil_frame.c tests/test_soil_frame.c && /tmp/t
#include "../soil_frame.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) \
    do { \
        if(!(c)) { \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            failures++; \
        } \
    } while(0)

static int feed_all(SoilParser* p, const uint8_t* d, size_t n, SoilReading* last) {
    int frames = 0;
    for(size_t i = 0; i < n; i++) {
        if(soil_parser_feed(p, d[i], last)) frames++;
    }
    return frames;
}

int main(void) {
    SoilParser p;
    SoilReading r = {0};

    // CRC-16/CCITT-FALSE check value
    CHECK(soil_crc16_ccitt_false((const uint8_t*)"123456789", 9) == 0x29B1);

    // captures/burst_hex.txt: 3 frames, then one split in two reads
    static const uint8_t burst[] = {
        0xA5, 0x04, 0x2C, 0x01, 0xE1, 0x00, 0xC5, 0xF4, 0xA5, 0x04, 0x2D, 0x01,
        0xE2, 0x00, 0x22, 0xD7, 0xA5, 0x04, 0x2E, 0x01, 0xE3, 0x00, 0xCF, 0x7F,
        0xA5, 0x04, 0x2F, 0x01, /* split */ 0xE4, 0x00, 0xEC, 0x90};
    soil_parser_reset(&p);
    CHECK(feed_all(&p, burst, sizeof(burst), &r) == 4);
    CHECK(r.moisture == 0x12F && r.temperature_dc == 0xE4);
    CHECK(p.errors == 0 && p.skipped_bytes == 0);

    // captures/noise_hex.txt: garbage, over-long length, bad CRC, then valid frame
    static const uint8_t noise[] = {
        0xFF, 0x00, 0x13, 0xA5, 0xC8, 0x01, 0x02, 0x03, 0xA5, 0x04, 0x2C, 0x01,
        0xE1, 0x00, 0xC5, 0x0B, 0xA5, 0x04, 0x2C, 0x01, 0xF1, 0xFF, 0x46, 0xE9};
    soil_parser_reset(&p);
    int n = feed_all(&p, noise, sizeof(noise), &r);
    printf("noise: frames=%d errors=%u skipped=%u moisture=%u temp=%d\n", n, p.errors,
           p.skipped_bytes, r.moisture, r.temperature_dc);
    CHECK(n >= 1);
    CHECK(p.errors >= 2);

    // Byte-at-a-time arrival equals bulk arrival (already byte-wise); also bound check
    soil_parser_reset(&p);
    uint8_t flood[1000];
    memset(flood, SOIL_SYNC_BYTE, sizeof(flood)); // worst case: all sync bytes
    feed_all(&p, flood, sizeof(flood), &r);
    CHECK(p.fill <= SOIL_FRAME_MAX);

    printf(failures ? "FAILED\n" : "ALL PASSED\n");
    return failures != 0;
}
