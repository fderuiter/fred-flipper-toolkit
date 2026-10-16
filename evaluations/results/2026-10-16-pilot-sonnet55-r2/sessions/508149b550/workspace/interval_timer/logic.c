#include "logic.h"

#include <stdio.h>
#include <string.h>

void settings_set_defaults(Settings* s) {
    s->work = 30;
    s->rest = 10;
    s->rounds = 8;
    s->vibrate = true;
}

static bool parse_uint(const char* p, size_t n, uint32_t* out) {
    if(n == 0 || n > 9) return false; /* 9 digits cannot overflow uint32 */
    uint32_t v = 0;
    for(size_t i = 0; i < n; i++) {
        if(p[i] < '0' || p[i] > '9') return false;
        v = v * 10 + (uint32_t)(p[i] - '0');
    }
    *out = v;
    return true;
}

typedef struct {
    const char* key;
    size_t key_len;
    const char* val;
    size_t val_len;
} Line;

/* Splits "Key: value". Trailing '\r' and spaces are trimmed from the value. */
static bool split_line(const char* p, size_t n, Line* l) {
    while(n > 0 && (p[n - 1] == '\r' || p[n - 1] == ' ')) n--;
    size_t colon = 0;
    while(colon < n && p[colon] != ':') colon++;
    if(colon == 0 || colon >= n) return false;
    l->key = p;
    l->key_len = colon;
    size_t v = colon + 1;
    while(v < n && p[v] == ' ') v++;
    l->val = p + v;
    l->val_len = n - v;
    return true;
}

static bool key_is(const Line* l, const char* k) {
    return l->key_len == strlen(k) && memcmp(l->key, k, l->key_len) == 0;
}

static bool in_range(uint32_t v, uint32_t lo, uint32_t hi) {
    return v >= lo && v <= hi;
}

SettingsParseResult settings_parse(const char* buf, size_t len, Settings* out) {
    Settings tmp;
    settings_set_defaults(&tmp);
    settings_set_defaults(out);

    if(len == 0 || len > SETTINGS_MAX_FILE_BYTES) return SettingsParseCorrupt;

    uint32_t version = 0;
    bool seen_work = false, seen_rest = false, seen_rounds = false, seen_vib = false;
    unsigned line_no = 0;
    size_t pos = 0;

    while(pos < len) {
        size_t end = pos;
        while(end < len && buf[end] != '\n') end++;
        size_t n = end - pos;
        const char* p = buf + pos;
        pos = end + 1;

        if(n > SETTINGS_MAX_LINE_BYTES) return SettingsParseCorrupt;
        if(n == 0 || (n == 1 && p[0] == '\r')) continue; /* blank line */

        Line l;
        if(!split_line(p, n, &l)) return SettingsParseCorrupt;
        line_no++;

        if(line_no == 1) {
            if(!key_is(&l, "Filetype") || l.val_len != strlen(SETTINGS_FILETYPE) ||
               memcmp(l.val, SETTINGS_FILETYPE, l.val_len) != 0)
                return SettingsParseCorrupt;
            continue;
        }
        if(line_no == 2) {
            if(!key_is(&l, "Version") || !parse_uint(l.val, l.val_len, &version) || version == 0)
                return SettingsParseCorrupt;
            /* Decide the future policy before validating the body. */
            if(version > SETTINGS_VERSION) return SettingsParseFuture;
            continue;
        }

        uint32_t v;
        if(key_is(&l, "Work")) {
            if(seen_work || !parse_uint(l.val, l.val_len, &v) || !in_range(v, WORK_MIN, WORK_MAX))
                return SettingsParseCorrupt;
            tmp.work = (uint16_t)v;
            seen_work = true;
        } else if(key_is(&l, "Rest")) {
            if(seen_rest || !parse_uint(l.val, l.val_len, &v) || !in_range(v, REST_MIN, REST_MAX))
                return SettingsParseCorrupt;
            tmp.rest = (uint16_t)v;
            seen_rest = true;
        } else if(version >= 2 && key_is(&l, "Rounds")) {
            if(seen_rounds || !parse_uint(l.val, l.val_len, &v) ||
               !in_range(v, ROUNDS_MIN, ROUNDS_MAX))
                return SettingsParseCorrupt;
            tmp.rounds = (uint16_t)v;
            seen_rounds = true;
        } else if(version >= 2 && key_is(&l, "Vibrate")) {
            if(seen_vib) return SettingsParseCorrupt;
            if(l.val_len == 4 && memcmp(l.val, "true", 4) == 0)
                tmp.vibrate = true;
            else if(l.val_len == 5 && memcmp(l.val, "false", 5) == 0)
                tmp.vibrate = false;
            else
                return SettingsParseCorrupt;
            seen_vib = true;
        }
        /* other keys: ignored */
    }

    if(line_no < 2 || version == 0) return SettingsParseCorrupt;
    if(!seen_work || !seen_rest) return SettingsParseCorrupt;
    if(version >= 2 && (!seen_rounds || !seen_vib)) return SettingsParseCorrupt;

    *out = tmp;
    return version == SETTINGS_VERSION ? SettingsParseOk : SettingsParseMigrated;
}

size_t settings_format(const Settings* s, char* buf, size_t cap) {
    int n = snprintf(
        buf,
        cap,
        "Filetype: %s\nVersion: %d\nWork: %u\nRest: %u\nRounds: %u\nVibrate: %s\n",
        SETTINGS_FILETYPE,
        SETTINGS_VERSION,
        (unsigned)s->work,
        (unsigned)s->rest,
        (unsigned)s->rounds,
        s->vibrate ? "true" : "false");
    if(n <= 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

uint16_t ladder_value(uint8_t i) {
    if(i >= LADDER_COUNT) i = LADDER_COUNT - 1;
    if(i <= 60) return i;
    if(i <= 108) return (uint16_t)(60 + (i - 60) * 5);
    return (uint16_t)(300 + (i - 108) * 10);
}

uint8_t ladder_nearest(uint16_t value, uint8_t first_index) {
    uint8_t best = first_index;
    uint16_t best_d = 0xFFFF;
    for(uint8_t i = first_index; i < LADDER_COUNT; i++) {
        uint16_t v = ladder_value(i);
        uint16_t d = v > value ? v - value : value - v;
        if(d < best_d) {
            best_d = d;
            best = i;
        }
    }
    return best;
}

void timer_progress(const Settings* s, uint32_t elapsed_ms, Progress* p) {
    const uint32_t work_ms = (uint32_t)s->work * 1000u;
    const uint32_t rest_ms = (uint32_t)s->rest * 1000u;
    const uint32_t cycle = work_ms + rest_ms; /* >= 5000 */
    /* max 99*600s + 98*600s = 118 200 s = 118 200 000 ms: fits uint32 */
    const uint32_t total = (uint32_t)s->rounds * work_ms + (uint32_t)(s->rounds - 1) * rest_ms;

    if(elapsed_ms >= total) {
        p->phase = PhaseDone;
        p->round = s->rounds;
        p->remaining_s = 0;
        return;
    }
    const uint32_t r = elapsed_ms / cycle;
    const uint32_t off = elapsed_ms % cycle;
    p->round = (uint16_t)(r + 1);
    if(off < work_ms) {
        p->phase = PhaseWork;
        p->remaining_s = (work_ms - off + 999) / 1000;
    } else {
        p->phase = PhaseRest;
        p->remaining_s = (cycle - off + 999) / 1000;
    }
}
