/* Host test: gcc -Wall -Wextra -fsanitize=address,undefined -I.. test_settings.c ../settings_logic.c
 * Run: ./a.out ../../samples */
#include "settings_logic.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c) \
    do { \
        checks++; \
        if(!(c)) { \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            return 1; \
        } \
    } while(0)

static ITLoadStatus parse_str(const char* s, ITSettings* o) {
    return it_settings_parse(s, strlen(s), o);
}

static int parse_file(const char* dir, const char* name, ITSettings* o, ITLoadStatus* st) {
    char path[256];
    char buf[IT_SETTINGS_MAX_BYTES + 1];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) return 1;
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    *st = it_settings_parse(buf, n, o);
    return 0;
}

int main(int argc, char** argv) {
    ITSettings s;
    const char* H = "Filetype: Interval Timer Settings\n";

    if(argc > 1) {
        ITLoadStatus st;
        CHECK(parse_file(argv[1], "settings_v1.txt", &s, &st) == 0);
        CHECK(st == ITLoadMigratedV1 && s.work == 45 && s.rest == 15);
        CHECK(s.rounds == IT_ROUNDS_DEFAULT && s.vibrate == IT_VIBRATE_DEFAULT);

        CHECK(parse_file(argv[1], "settings_v2.txt", &s, &st) == 0);
        CHECK(st == ITLoadOk && s.work == 30 && s.rest == 10 && s.rounds == 8 && s.vibrate);

        CHECK(parse_file(argv[1], "settings_v2_corrupt.txt", &s, &st) == 0);
        CHECK(st == ITLoadRepaired);
        CHECK(s.work == IT_WORK_DEFAULT && s.rest == IT_REST_DEFAULT);
        CHECK(s.rounds == IT_ROUNDS_DEFAULT && s.vibrate == IT_VIBRATE_DEFAULT);

        CHECK(parse_file(argv[1], "settings_v7_future.txt", &s, &st) == 0);
        CHECK(st == ITLoadFuture && it_load_status_is_readonly(st));
        CHECK(s.work == 30 && s.rest == 10 && s.rounds == 8 && s.vibrate);
    } else {
        printf("note: sample dir not given, sample-file cases skipped\n");
    }

    /* v1 boundary values survive migration */
    char b[256];
    snprintf(b, sizeof(b), "%sVersion: 1\nWork: 5\nRest: 0\n", H);
    CHECK(parse_str(b, &s) == ITLoadMigratedV1 && s.work == 5 && s.rest == 0);
    snprintf(b, sizeof(b), "%sVersion: 1\r\nWork: 600\r\nRest: 600\r\n", H);
    CHECK(parse_str(b, &s) == ITLoadMigratedV1 && s.work == 600 && s.rest == 600);
    /* v1 with out-of-range Work: only that field defaults */
    snprintf(b, sizeof(b), "%sVersion: 1\nWork: 4\nRest: 20\n", H);
    CHECK(parse_str(b, &s) == ITLoadRepaired && s.work == IT_WORK_DEFAULT && s.rest == 20);

    /* range edges for v2 */
    snprintf(b, sizeof(b), "%sVersion: 2\nWork: 601\nRest: 601\nRounds: 100\nVibrate: false\n", H);
    CHECK(parse_str(b, &s) == ITLoadRepaired);
    CHECK(s.work == IT_WORK_DEFAULT && s.rest == IT_REST_DEFAULT && s.rounds == IT_ROUNDS_DEFAULT);
    CHECK(!s.vibrate);
    snprintf(b, sizeof(b), "%sVersion: 2\nWork: 600\nRest: 0\nRounds: 99\nVibrate: false\n", H);
    CHECK(parse_str(b, &s) == ITLoadOk && s.work == 600 && s.rest == 0 && s.rounds == 99);
    snprintf(b, sizeof(b), "%sVersion: 2\nWork: 5\nRest: 0\nRounds: 1\nVibrate: true\n", H);
    CHECK(parse_str(b, &s) == ITLoadOk && s.work == 5 && s.rounds == 1);

    /* number hygiene: sign, hex, junk, leading plus, empty, 6 digits */
    const char* bad[] = {"-1", "+5", "0x10", "12a", "", "000030", " ", "3 0"};
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(b, sizeof(b), "%sVersion: 2\nWork: %s\nRest: 1\nRounds: 2\nVibrate: true\n", H, bad[i]);
        CHECK(parse_str(b, &s) == ITLoadRepaired);
        CHECK(s.work == IT_WORK_DEFAULT && s.rest == 1 && s.rounds == 2);
    }

    /* missing field in v2, duplicate key (last wins), unknown key, long line */
    snprintf(b, sizeof(b), "%sVersion: 2\nWork: 20\nWork: 25\nExtra: 1\nRest: 3\n", H);
    CHECK(parse_str(b, &s) == ITLoadRepaired && s.work == 25 && s.rest == 3);
    char longline[200];
    memset(longline, 'x', sizeof(longline) - 1);
    longline[sizeof(longline) - 1] = 0;
    snprintf(b, sizeof(b), "%sVersion: 2\nWork: 20\nJunk: %s\nRest: 3\nRounds: 2\nVibrate: true\n", H, longline);
    CHECK(parse_str(b, &s) == ITLoadOk && s.work == 20);

    /* header problems -> Corrupt with defaults */
    CHECK(parse_str("", &s) == ITLoadCorrupt && s.work == IT_WORK_DEFAULT);
    CHECK(parse_str("Filetype: Other\nVersion: 2\nWork: 9\n", &s) == ITLoadCorrupt);
    CHECK(parse_str("Filetype: Interval Timer Settings\n", &s) == ITLoadCorrupt);
    CHECK(parse_str("Filetype: Interval Timer Settings\nVersion: 0\n", &s) == ITLoadCorrupt);
    CHECK(parse_str("Filetype: Interval Timer Settings\nVersion: x\n", &s) == ITLoadCorrupt);
    CHECK(parse_str("Filetype: Interval Timer Settings\nWork: 9\n", &s) == ITLoadCorrupt);
    CHECK(parse_str("Filetype: Interval Time", &s) == ITLoadCorrupt);
    CHECK(!it_load_status_is_readonly(ITLoadCorrupt));

    /* v1 file must not pick up Rounds/Vibrate */
    snprintf(b, sizeof(b), "%sVersion: 1\nWork: 9\nRest: 1\nRounds: 50\nVibrate: false\n", H);
    CHECK(parse_str(b, &s) == ITLoadMigratedV1 && s.rounds == IT_ROUNDS_DEFAULT && s.vibrate);

    /* oversize -> read-only */
    static char big[IT_SETTINGS_MAX_BYTES + 10];
    memset(big, 'a', sizeof(big));
    CHECK(it_settings_parse(big, IT_SETTINGS_MAX_BYTES + 1, &s) == ITLoadTooLarge);
    CHECK(it_load_status_is_readonly(ITLoadTooLarge));

    /* truncation at every prefix of a valid file never crashes, never yields out-of-range */
    ITSettings src = {.work = 123, .rest = 45, .rounds = 6, .vibrate = false};
    char full[IT_SETTINGS_MAX_BYTES];
    size_t flen = it_settings_format(&src, full, sizeof(full));
    CHECK(flen > 0);
    for(size_t i = 0; i <= flen; i++) {
        it_settings_parse(full, i, &s);
        CHECK(s.work >= IT_WORK_MIN && s.work <= IT_WORK_MAX);
        CHECK(s.rest <= IT_REST_MAX);
        CHECK(s.rounds >= IT_ROUNDS_MIN && s.rounds <= IT_ROUNDS_MAX);
    }

    /* round trip + format bounds */
    CHECK(it_settings_parse(full, flen, &s) == ITLoadOk);
    CHECK(s.work == 123 && s.rest == 45 && s.rounds == 6 && !s.vibrate);
    char tiny[10];
    CHECK(it_settings_format(&src, tiny, sizeof(tiny)) == 0);
    CHECK(it_settings_format(&src, full, flen) == 0); /* no room for NUL */
    CHECK(it_settings_format(&src, full, flen + 1) == flen);
    ITSettings invalid = {.work = 4, .rest = 0, .rounds = 1, .vibrate = true};
    CHECK(it_settings_format(&invalid, full, sizeof(full)) == 0);

    printf("OK %d checks\n", checks);
    return 0;
}
