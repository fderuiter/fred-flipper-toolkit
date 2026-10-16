/* Host tests for interval_logic.c. Build: gcc -Wall -Wextra -I.. host_test.c ../interval_logic.c -o host_test */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "interval_logic.h"

static ItParseResult P(const char* s, ItSettings* o) {
    return it_settings_parse(s, strlen(s), o);
}

int main(void) {
    ItSettings s;

    /* v1 -> migrated, Work/Rest preserved, new fields defaulted */
    assert(P("Filetype: Interval Timer Settings\nVersion: 1\nWork: 45\nRest: 15\n", &s) ==
           ItParseMigrated);
    assert(s.work_s == 45 && s.rest_s == 15 && s.rounds == IT_ROUNDS_DEFAULT && s.vibrate);

    /* v1 with CRLF and no trailing newline */
    assert(P("Filetype: Interval Timer Settings\r\nVersion: 1\r\nWork: 5\r\nRest: 0", &s) ==
           ItParseMigrated);
    assert(s.work_s == 5 && s.rest_s == 0);

    /* v2 ok */
    assert(P("Filetype: Interval Timer Settings\nVersion: 2\nWork: 30\nRest: 10\nRounds: 8\nVibrate: false\n", &s) ==
           ItParseOk);
    assert(s.work_s == 30 && s.rest_s == 10 && s.rounds == 8 && !s.vibrate);

    /* corrupt sample: out-of-range, negative, zero rounds, truncated -> defaults */
    assert(P("Filetype: Interval Timer Settings\nVersion: 2\nWork: 99999999999\nRest: -4\nRounds: 0\nVib", &s) ==
           ItParseInvalid);
    assert(s.work_s == IT_WORK_DEFAULT && s.rest_s == IT_REST_DEFAULT);

    /* truncated bool, missing key, duplicate key, bad header, bounds */
    assert(P("Filetype: Interval Timer Settings\nVersion: 2\nWork: 30\nRest: 10\nRounds: 8\nVibrate: tru", &s) == ItParseInvalid);
    assert(P("Filetype: Interval Timer Settings\nVersion: 2\nWork: 30\nRest: 10\n", &s) == ItParseInvalid);
    assert(P("Filetype: Interval Timer Settings\nVersion: 1\nWork: 30\nWork: 31\nRest: 10\n", &s) == ItParseInvalid);
    assert(P("Filetype: Other\nVersion: 1\nWork: 30\nRest: 10\n", &s) == ItParseInvalid);
    assert(P("Version: 1\nWork: 30\nRest: 10\n", &s) == ItParseInvalid);
    assert(P("Filetype: Interval Timer Settings\nVersion: 1\nWork: 4\nRest: 10\n", &s) == ItParseInvalid);
    assert(P("Filetype: Interval Timer Settings\nVersion: 1\nWork: 601\nRest: 10\n", &s) == ItParseInvalid);
    assert(P("Filetype: Interval Timer Settings\nVersion: 1\nWork: 30\nRest: 601\n", &s) == ItParseInvalid);
    assert(P("Filetype: Interval Timer Settings\nVersion: 0\nWork: 30\nRest: 10\n", &s) == ItParseInvalid);
    assert(P("", &s) == ItParseInvalid);
    assert(it_settings_parse("Filetype: Interval Timer Settings\nVersion: 1\0\nWork: 30\nRest: 1\n", 62, &s) == ItParseInvalid);

    /* oversize input */
    static char big[IT_FILE_MAX_SIZE + 10];
    memset(big, 'a', sizeof(big));
    assert(it_settings_parse(big, sizeof(big), &s) == ItParseInvalid);

    /* future version: best-effort values, unknown key ignored, flagged */
    assert(P("Filetype: Interval Timer Settings\nVersion: 7\nWork: 30\nRest: 10\nRounds: 8\nVibrate: true\nProfile: hiit\n", &s) == ItParseFuture);
    assert(s.work_s == 30 && s.rest_s == 10 && s.rounds == 8 && s.vibrate);
    assert(P("Filetype: Interval Timer Settings\nVersion: 7\nWork: 9999\n", &s) == ItParseFuture);
    assert(s.work_s == IT_WORK_DEFAULT);

    /* serialize round trip */
    char buf[IT_FILE_MAX_SIZE];
    ItSettings in = {.work_s = 600, .rest_s = 0, .rounds = 99, .vibrate = false};
    size_t n = it_settings_serialize(&in, buf, sizeof(buf));
    assert(n > 0);
    assert(it_settings_parse(buf, n, &s) == ItParseOk);
    assert(s.work_s == 600 && s.rest_s == 0 && s.rounds == 99 && !s.vibrate);
    assert(it_settings_serialize(&in, buf, 10) == 0);
    in.rounds = 0;
    assert(it_settings_serialize(&in, buf, sizeof(buf)) == 0);

    /* session: 2 rounds, work 5, rest 3 -> W R W done */
    ItSettings cfg = {.work_s = 5, .rest_s = 3, .rounds = 2, .vibrate = true};
    ItSession ss;
    it_session_start(&ss, &cfg);
    assert(ss.phase == ItPhaseWork && ss.round == 1 && it_session_seconds_left(&ss) == 5);
    assert(it_session_step(&ss, &cfg, 1500) == 0 && it_session_seconds_left(&ss) == 4);
    assert(it_session_step(&ss, &cfg, 3500) == IT_STEP_PHASE_CHANGED && ss.phase == ItPhaseRest);
    assert(it_session_step(&ss, &cfg, 3000) == IT_STEP_PHASE_CHANGED && ss.phase == ItPhaseWork &&
           ss.round == 2);
    assert(it_session_step(&ss, &cfg, 5000) == (IT_STEP_PHASE_CHANGED | IT_STEP_FINISHED) &&
           ss.phase == ItPhaseDone);
    assert(it_session_step(&ss, &cfg, 1000) == 0);

    /* rest 0 skips rest; huge elapsed finishes cleanly with overshoot carried */
    cfg.rest_s = 0;
    cfg.rounds = 3;
    it_session_start(&ss, &cfg);
    assert(it_session_step(&ss, &cfg, 5000) == IT_STEP_PHASE_CHANGED && ss.phase == ItPhaseWork &&
           ss.round == 2);
    assert((it_session_step(&ss, &cfg, 0xFFFFFFFFu) & IT_STEP_FINISHED) && ss.phase == ItPhaseDone);

    puts("all host tests passed");
    return 0;
}
