// Host test: gcc -Wall -Wextra -I.. test_soil_frame.c ../soil_frame.c -o t && ./t
#include "soil_frame.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) \
    do { \
        if(!(c)) { \
            printf("FAIL line %d: %s\n", __LINE__, #c); \
            failures++; \
        } \
    } while(0)

static int feed_all(SoilParser* p, const uint8_t* d, size_t n, SoilReading* last) {
    int got = 0;
    for(size_t i = 0; i < n; i++) {
        if(soil_parser_feed(p, d[i], last)) got++;
    }
    return got;
}

int main(void) {
    SoilReading r = {0};
    SoilParser p;

    CHECK(soil_crc16((const uint8_t*)"123456789", 9) == 0x29B1); // CCITT-FALSE check value

    // burst capture: three frames back to back, fourth split across reads
    static const uint8_t burst[] = {
        0xA5, 0x04, 0x2C, 0x01, 0xE1, 0x00, 0xC5, 0xF4, 0xA5, 0x04, 0x2D, 0x01, 0xE2, 0x00, 0x22,
        0xD7, 0xA5, 0x04, 0x2E, 0x01, 0xE3, 0x00, 0xCF, 0x7F, 0xA5, 0x04, 0x2F, 0x01, 0xE4, 0x00,
        0xEC, 0x90};
    soil_parser_reset(&p);
    CHECK(feed_all(&p, burst, sizeof(burst), &r) == 4);
    CHECK(p.error_count == 0);
    CHECK(r.moisture == 0x012F && r.temperature_dc == 0x00E4);

    // noise capture: garbage, over-long length, bad CRC, then valid frame
    static const uint8_t noise[] = {
        0xFF, 0x00, 0x13, 0xA5, 0xC8, 0x01, 0x02, 0x03, 0xA5, 0x04, 0x2C, 0x01, 0xE1, 0x00,
        0xC5, 0x0B, 0xA5, 0x04, 0x2C, 0x01, 0xF1, 0xFF, 0x46, 0xE9};
    soil_parser_reset(&p);
    int got = feed_all(&p, noise, sizeof(noise), &r);
    printf("noise: frames=%d errors=%u moisture=%u temp=%d\n", got, (unsigned)p.error_count,
           r.moisture, r.temperature_dc);
    CHECK(got == 1);
    CHECK(r.moisture == 300 && r.temperature_dc == -15);
    CHECK(p.error_count >= 3);

    // valid frame hidden inside a bad-CRC frame is recovered by rescan
    static const uint8_t nested[] = {0xA5, 0x08, 0xA5, 0x04, 0x2C, 0x01, 0xE1, 0x00, 0xC5, 0xF4, 0x00, 0x00};
    soil_parser_reset(&p);
    CHECK(feed_all(&p, nested, sizeof(nested), &r) == 1);

    // max payload (32, reserved bytes) accepted; moisture > 1000 and short payload rejected
    uint8_t f[SOIL_MAX_FRAME] = {SOIL_SYNC, 32, 0xE8, 0x03, 0x00, 0x00};
    uint16_t c = soil_crc16(f + 1, 33);
    f[34] = (uint8_t)c;
    f[35] = (uint8_t)(c >> 8);
    soil_parser_reset(&p);
    CHECK(feed_all(&p, f, 36, &r) == 1 && r.moisture == 1000);

    uint8_t hi[] = {SOIL_SYNC, 4, 0xE9, 0x03, 0, 0, 0, 0};
    c = soil_crc16(hi + 1, 5);
    hi[6] = (uint8_t)c;
    hi[7] = (uint8_t)(c >> 8);
    soil_parser_reset(&p);
    CHECK(feed_all(&p, hi, sizeof(hi), &r) == 0 && p.error_count == 1);

    uint8_t sh[] = {SOIL_SYNC, 2, 0x01, 0x00, 0, 0};
    c = soil_crc16(sh + 1, 3);
    sh[4] = (uint8_t)c;
    sh[5] = (uint8_t)(c >> 8);
    soil_parser_reset(&p);
    CHECK(feed_all(&p, sh, sizeof(sh), &r) == 0 && p.error_count == 1);

    // all-sync flood and random bytes never overrun the buffer (run under -fsanitize)
    soil_parser_reset(&p);
    for(int i = 0; i < 10000; i++) soil_parser_feed(&p, (i % 3) ? 0xA5 : (uint8_t)(i * 31), &r);
    CHECK(p.len < SOIL_MAX_FRAME);

    printf(failures ? "FAILED\n" : "all passed\n");
    return failures != 0;
}
