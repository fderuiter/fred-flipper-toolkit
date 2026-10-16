// Host test for the H01 reference stopwatch logic.
// Build: cc -std=c99 -Wall -Wextra -Werror stopwatch_core.c test_stopwatch_core.c
#include "stopwatch_core.h"
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c)                                                         \
    do {                                                                 \
        checks++;                                                        \
        if(!(c)) {                                                       \
            failures++;                                                  \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                \
    } while(0)

static void fmt_is(uint32_t ms, const char* want) {
    char b[16];
    CHECK(stopwatch_format(ms, b, sizeof b));
    CHECK(strcmp(b, want) == 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Stopwatch sw;
    stopwatch_reset(&sw);
    char small[8];
    CHECK(!stopwatch_format(0, small, sizeof small));
    fmt_is(0, "00:00.00");
    fmt_is(1234, "00:01.23");
    fmt_is(61009, "01:01.00");
    fmt_is(UINT32_MAX, "99:59.99");

    /* lap ignored while stopped, reset refused while running */
    CHECK(!stopwatch_lap(&sw, 0));
    stopwatch_toggle(&sw, 1000);
    CHECK(sw.running);
    CHECK(!stopwatch_long_ok(&sw));
    CHECK(stopwatch_elapsed(&sw, 3500) == 2500);
    stopwatch_toggle(&sw, 3500);
    CHECK(stopwatch_elapsed(&sw, 99999) == 2500); /* stopped: frozen */
    stopwatch_toggle(&sw, 100000);
    CHECK(stopwatch_elapsed(&sw, 100500) == 3000);

    /* ring keeps the last 10, newest first */
    for(uint32_t i = 1; i <= 12; i++) CHECK(stopwatch_lap(&sw, 100000 + i * 100));
    CHECK(sw.lap_count == STOPWATCH_LAPS);
    uint32_t v = 0;
    CHECK(stopwatch_lap_get(&sw, 0, &v) && v == 2500 + 1200);
    CHECK(stopwatch_lap_get(&sw, 9, &v) && v == 2500 + 300);
    CHECK(!stopwatch_lap_get(&sw, 10, &v));

    /* tick wrap during a run */
    Stopwatch w;
    stopwatch_reset(&w);
    stopwatch_toggle(&w, UINT32_MAX - 499u);
    CHECK(stopwatch_elapsed(&w, 500u) == 1000u);

    stopwatch_toggle(&sw, 200000);
    CHECK(stopwatch_long_ok(&sw));
    CHECK(sw.lap_count == 0 && stopwatch_elapsed(&sw, 5) == 0);

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
