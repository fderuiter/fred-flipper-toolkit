// Host test for the task 03 reference frame parser over the fixture captures.
// Build: cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined soil_frame.c test_soil_frame.c
// Run:   ./a.out ../../fixtures/03-uart/captures
#include "soil_frame.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    SoilReading r[16];
    size_t n;
} Sink;

static void on_reading(const SoilReading* r, void* ctx) {
    Sink* s = ctx;
    if(s->n < 16) s->r[s->n++] = *r;
}

/* Feed a hex capture line by line (each line = one UART read). '#' starts a comment line. */
static int feed_capture(const char* path, SoilParser* p, Sink* s) {
    FILE* f = fopen(path, "r");
    if(!f) return -1;
    char line[1024];
    while(fgets(line, sizeof(line), f)) {
        if(line[0] == '#') continue;
        uint8_t bytes[256];
        size_t n = 0;
        for(char* tok = strtok(line, " \r\n"); tok && n < sizeof(bytes); tok = strtok(NULL, " \r\n"))
            bytes[n++] = (uint8_t)strtoul(tok, NULL, 16);
        soil_parser_feed(p, bytes, n, on_reading, s);
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    char path[512];
    int failures = 0;
    SoilParser p;
    Sink s;

    uint8_t check[] = "123456789";
    if(soil_crc16_ccitt_false(check, 9) != 0x29B1) {
        printf("FAIL crc check value\n");
        failures++;
    }

    soil_parser_reset(&p);
    memset(&s, 0, sizeof(s));
    snprintf(path, sizeof(path), "%s/burst_hex.txt", dir);
    if(feed_capture(path, &p, &s) != 0) return 2;
    int ok = s.n == 4 && p.stats.bad_crc == 0 && p.stats.bad_length == 0;
    for(size_t i = 0; ok && i < 4; i++)
        ok = s.r[i].moisture == 300 + i && s.r[i].temp_decicelsius == (int16_t)(225 + i);
    printf("%s burst: %zu readings, errors=%u\n", ok ? "PASS" : "FAIL", s.n,
           (unsigned)(p.stats.bad_crc + p.stats.bad_length));
    failures += !ok;

    soil_parser_reset(&p);
    memset(&s, 0, sizeof(s));
    snprintf(path, sizeof(path), "%s/noise_hex.txt", dir);
    if(feed_capture(path, &p, &s) != 0) return 2;
    uint32_t errors = p.stats.bad_crc + p.stats.bad_length + p.stats.bad_payload;
    ok = s.n == 1 && s.r[0].moisture == 300 && s.r[0].temp_decicelsius == -15 && errors >= 2;
    printf("%s noise: %zu reading(s), bad_length=%u bad_crc=%u\n", ok ? "PASS" : "FAIL", s.n,
           (unsigned)p.stats.bad_length, (unsigned)p.stats.bad_crc);
    failures += !ok;

    /* Fuzz: random bytes never crash or exceed buffers (ASan/UBSan). */
    soil_parser_reset(&p);
    srand(1234);
    for(int i = 0; i < 200000; i++) {
        uint8_t b = (uint8_t)(rand() & 0xFF);
        if((i % 7) == 0) b = SOIL_SYNC;
        soil_parser_feed(&p, &b, 1, NULL, NULL);
    }
    printf("PASS random stream: 200000 bytes, frames_ok=%u resyncs=%u\n", (unsigned)p.stats.frames_ok,
           (unsigned)p.stats.resyncs);

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
