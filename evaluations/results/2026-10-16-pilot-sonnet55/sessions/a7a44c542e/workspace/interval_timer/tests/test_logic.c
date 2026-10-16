/* Host test: gcc -Wall -Wextra -I.. test_logic.c ../it_logic.c -o test_logic && ./test_logic [samples_dir] */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "it_logic.h"

static ItLoadResult parse_str(const char* s, ItSettings* o) {
    return it_settings_parse(s, strlen(s), o);
}

static void test_v1(void) {
    ItSettings s;
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: 45\nRest: 15\n", &s) == ItLoadMigrated);
    assert(s.work == 45 && s.rest == 15);
    assert(s.rounds == 8 && s.vibrate == true); /* defaults for new fields */
}

static void test_v2(void) {
    ItSettings s;
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 2\nWork: 30\nRest: 0\nRounds: 99\nVibrate: false\n", &s) == ItLoadOk);
    assert(s.work == 30 && s.rest == 0 && s.rounds == 99 && !s.vibrate);
}

static void test_corrupt(void) {
    ItSettings s, d;
    it_settings_defaults(&d);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 2\nWork: 99999999999\nRest: -4\nRounds: 0\nVib", &s) == ItLoadRepaired);
    assert(s.work == d.work && s.rest == d.rest && s.rounds == d.rounds && s.vibrate == d.vibrate);
    /* one bad field does not discard good ones */
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 2\nWork: 601\nRest: 7\nRounds: 3\nVibrate: true\n", &s) == ItLoadRepaired);
    assert(s.work == d.work && s.rest == 7 && s.rounds == 3);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 2\nWork: 4\n", &s) == ItLoadRepaired && s.work == d.work);
}

static void test_future_and_bad(void) {
    ItSettings s;
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 7\nWork: 31\nProfile: hiit\n", &s) == ItLoadFutureVersion);
    assert(s.work == 30); /* defaults, not the future file's values */
    assert(parse_str("Filetype: Other\nVersion: 1\n", &s) == ItLoadBadFile);
    assert(parse_str("Work: 10\n", &s) == ItLoadBadFile);
    assert(parse_str("", &s) == ItLoadBadFile);
    char big[IT_FILE_MAX_SIZE + 1];
    memset(big, 'a', sizeof(big));
    assert(it_settings_parse(big, sizeof(big), &s) == ItLoadBadFile);
    /* long line, CRLF */
    assert(parse_str("Filetype: Interval Timer Settings\r\nVersion: 2\r\nWork: 20\r\nRest: 5\r\nRounds: 2\r\nVibrate: true\r\n", &s) == ItLoadOk && s.work == 20);
}

static void test_roundtrip(void) {
    ItSettings a = {.work = 600, .rest = 0, .rounds = 1, .vibrate = false}, b;
    char buf[160];
    size_t n = it_settings_format(&a, buf, sizeof(buf));
    assert(n > 0);
    assert(it_settings_parse(buf, n, &b) == ItLoadOk);
    assert(b.work == 600 && b.rest == 0 && b.rounds == 1 && !b.vibrate);
    assert(it_settings_format(&a, buf, 10) == 0);
}

static void test_timer(void) {
    ItSettings c = {.work = 5, .rest = 2, .rounds = 2, .vibrate = true};
    ItTimer t;
    it_timer_start(&t, &c);
    int secs = 0, changes = 0, done = 0;
    while(t.phase != ItPhaseDone && secs < 1000) {
        ItEvent e = it_timer_step(&t);
        secs++;
        changes += e == ItEventPhaseChange;
        done += e == ItEventDone;
    }
    assert(secs == 5 + 2 + 5 && changes == 2 && done == 1); /* no rest after last round */
    assert(it_timer_step(&t) == ItEventNone);

    c.rest = 0;
    it_timer_start(&t, &c);
    secs = 0;
    while(t.phase != ItPhaseDone) {
        it_timer_step(&t);
        secs++;
    }
    assert(secs == 10);
}

int main(void) {
    test_v1();
    test_v2();
    test_corrupt();
    test_future_and_bad();
    test_roundtrip();
    test_timer();
    puts("all tests passed");
    return 0;
}
