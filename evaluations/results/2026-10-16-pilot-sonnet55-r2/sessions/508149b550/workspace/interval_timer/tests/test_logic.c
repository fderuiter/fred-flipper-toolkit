/* Host test: cc -std=c99 -Wall -Wextra -I.. ../logic.c test_logic.c -o /tmp/t && /tmp/t ../../samples */
#include "logic.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static SettingsParseResult parse_file(const char* dir, const char* name, Settings* s) {
    char path[256], buf[2048];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    assert(f);
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return settings_parse(buf, n, s);
}

static SettingsParseResult parse_str(const char* t, Settings* s) {
    return settings_parse(t, strlen(t), s);
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "../../samples";
    Settings s;

    /* v1 sample: values kept, new fields defaulted */
    assert(parse_file(dir, "settings_v1.txt", &s) == SettingsParseMigrated);
    assert(s.work == 45 && s.rest == 15 && s.rounds == 8 && s.vibrate);

    assert(parse_file(dir, "settings_v2.txt", &s) == SettingsParseOk);
    assert(s.work == 30 && s.rest == 10 && s.rounds == 8 && s.vibrate);

    assert(parse_file(dir, "settings_v2_corrupt.txt", &s) == SettingsParseCorrupt);
    assert(s.work == 30 && s.rest == 10); /* defaults */

    assert(parse_file(dir, "settings_v7_future.txt", &s) == SettingsParseFuture);

    /* off-grid v1 value is preserved exactly */
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: 47\nRest: 301\n", &s) ==
           SettingsParseMigrated);
    assert(s.work == 47 && s.rest == 301);

    /* CRLF, missing final newline */
    assert(parse_str("Filetype: Interval Timer Settings\r\nVersion: 1\r\nWork: 5\r\nRest: 0", &s) ==
           SettingsParseMigrated);
    assert(s.work == 5 && s.rest == 0);

    /* rejects */
    assert(parse_str("", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Other\nVersion: 1\nWork: 5\nRest: 0\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 0\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: 4\nRest: 0\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: 601\nRest: 0\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: 5\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: 5\nWork: 6\nRest: 1\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 2\nWork: 5\nRest: 1\nRounds: 100\nVibrate: true\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 2\nWork: 5\nRest: 1\nRounds: 3\nVibrate: yes\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 2\nWork: 5\nRest: 1\nRounds: 3\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: 99999999999\nRest: 1\n", &s) == SettingsParseCorrupt);
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 1\nWork: -5\nRest: 1\n", &s) == SettingsParseCorrupt);
    /* future version wins over body problems */
    assert(parse_str("Filetype: Interval Timer Settings\nVersion: 3\ngarbage\n", &s) == SettingsParseFuture);
    /* oversize */
    {
        char big[SETTINGS_MAX_FILE_BYTES + 10];
        memset(big, 'a', sizeof(big));
        assert(settings_parse(big, sizeof(big), &s) == SettingsParseCorrupt);
    }

    /* round trip */
    {
        char buf[160];
        Settings a = {.work = 600, .rest = 0, .rounds = 99, .vibrate = false}, b;
        size_t n = settings_format(&a, buf, sizeof(buf));
        assert(n > 0);
        assert(settings_parse(buf, n, &b) == SettingsParseOk);
        assert(b.work == 600 && b.rest == 0 && b.rounds == 99 && !b.vibrate);
        assert(settings_format(&a, buf, 10) == 0);
    }

    /* ladder */
    assert(ladder_value(0) == 0 && ladder_value(5) == 5 && ladder_value(60) == 60);
    assert(ladder_value(61) == 65 && ladder_value(108) == 300);
    assert(ladder_value(109) == 310 && ladder_value(LADDER_COUNT - 1) == 600);
    for(int i = 1; i < LADDER_COUNT; i++) assert(ladder_value(i) > ladder_value(i - 1));
    assert(ladder_nearest(47, LADDER_WORK_FIRST) == 47);
    assert(ladder_nearest(301, 0) == 108);
    assert(ladder_nearest(600, 0) == LADDER_COUNT - 1);
    assert(ladder_value(LADDER_WORK_FIRST) == WORK_MIN);

    /* timer: 2 rounds, work 10, rest 5 -> total 25 s, no rest after last round */
    {
        Settings t = {.work = 10, .rest = 5, .rounds = 2, .vibrate = false};
        Progress p;
        timer_progress(&t, 0, &p);
        assert(p.phase == PhaseWork && p.round == 1 && p.remaining_s == 10);
        timer_progress(&t, 1, &p);
        assert(p.remaining_s == 10);
        timer_progress(&t, 9999, &p);
        assert(p.phase == PhaseWork && p.remaining_s == 1);
        timer_progress(&t, 10000, &p);
        assert(p.phase == PhaseRest && p.round == 1 && p.remaining_s == 5);
        timer_progress(&t, 15000, &p);
        assert(p.phase == PhaseWork && p.round == 2 && p.remaining_s == 10);
        timer_progress(&t, 24999, &p);
        assert(p.phase == PhaseWork && p.round == 2 && p.remaining_s == 1);
        timer_progress(&t, 25000, &p);
        assert(p.phase == PhaseDone);
        timer_progress(&t, 0xFFFFFFFFu, &p);
        assert(p.phase == PhaseDone);

        Settings z = {.work = 5, .rest = 0, .rounds = 3, .vibrate = false};
        timer_progress(&z, 5000, &p);
        assert(p.phase == PhaseWork && p.round == 2 && p.remaining_s == 5);
        timer_progress(&z, 15000, &p);
        assert(p.phase == PhaseDone);

        Settings m = {.work = 600, .rest = 600, .rounds = 99, .vibrate = false};
        timer_progress(&m, 118199999u, &p);
        assert(p.phase == PhaseWork && p.round == 99 && p.remaining_s == 1);
        timer_progress(&m, 118200000u, &p);
        assert(p.phase == PhaseDone);
    }

    puts("all tests passed");
    return 0;
}
