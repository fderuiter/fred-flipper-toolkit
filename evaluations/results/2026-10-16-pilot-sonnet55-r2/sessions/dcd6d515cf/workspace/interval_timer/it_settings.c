#include "it_settings.h"

#include <stdio.h>
#include <string.h>

#define IT_MAX_LINE 64

void it_settings_defaults(ItSettings* s) {
    s->work = 30;
    s->rest = 10;
    s->rounds = 8;
    s->vibrate = true;
}

static bool parse_uint(const char* s, size_t n, uint32_t* out) {
    if(n == 0 || n > 9) return false; /* 9 digits cannot overflow uint32_t; sign rejected */
    uint32_t v = 0;
    for(size_t i = 0; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    *out = v;
    return true;
}

static bool key_is(const char* k, size_t kn, const char* name) {
    return strlen(name) == kn && memcmp(k, name, kn) == 0;
}

static bool in_range(uint32_t v, uint32_t lo, uint32_t hi) {
    return v >= lo && v <= hi;
}

ItParseResult it_settings_parse(const char* buf, size_t len, ItSettings* out) {
    if(len == 0 || len > IT_MAX_FILE) return ItParseCorrupt;
    if(memchr(buf, '\0', len)) return ItParseCorrupt;
    if(buf[len - 1] != '\n') return ItParseCorrupt; /* truncated */

    ItSettings tmp;
    it_settings_defaults(&tmp);

    uint32_t version = 0;
    bool seen_work = false, seen_rest = false, seen_rounds = false, seen_vib = false;
    size_t line_no = 0;
    size_t pos = 0;

    while(pos < len) {
        const char* line = buf + pos;
        const char* nl = memchr(line, '\n', len - pos);
        /* trailing newline guaranteed above, so nl != NULL */
        size_t n = (size_t)(nl - line);
        pos += n + 1;
        if(n > 0 && line[n - 1] == '\r') n--;
        if(n > IT_MAX_LINE) return ItParseCorrupt;
        if(n == 0 && line_no >= 2) continue; /* blank lines after header are ignored */

        const char* colon = memchr(line, ':', n);
        if(!colon || (size_t)(colon - line) + 1 >= n || colon[1] != ' ') return ItParseCorrupt;
        size_t kn = (size_t)(colon - line);
        const char* val = colon + 2;
        size_t vn = n - kn - 2;

        if(line_no == 0) {
            if(!key_is(line, kn, "Filetype")) return ItParseCorrupt;
            if(vn != strlen(IT_FILETYPE) || memcmp(val, IT_FILETYPE, vn) != 0)
                return ItParseCorrupt;
        } else if(line_no == 1) {
            if(!key_is(line, kn, "Version")) return ItParseCorrupt;
            if(!parse_uint(val, vn, &version) || version < 1) return ItParseCorrupt;
            /* Future check happens before body validation on purpose. */
            if(version > IT_VERSION_CURRENT) return ItParseFuture;
        } else if(key_is(line, kn, "Work")) {
            if(seen_work || !parse_uint(val, vn, &tmp.work)) return ItParseCorrupt;
            if(!in_range(tmp.work, IT_WORK_MIN, IT_WORK_MAX)) return ItParseCorrupt;
            seen_work = true;
        } else if(key_is(line, kn, "Rest")) {
            if(seen_rest || !parse_uint(val, vn, &tmp.rest)) return ItParseCorrupt;
            if(!in_range(tmp.rest, IT_REST_MIN, IT_REST_MAX)) return ItParseCorrupt;
            seen_rest = true;
        } else if(version >= 2 && key_is(line, kn, "Rounds")) {
            if(seen_rounds || !parse_uint(val, vn, &tmp.rounds)) return ItParseCorrupt;
            if(!in_range(tmp.rounds, IT_ROUNDS_MIN, IT_ROUNDS_MAX)) return ItParseCorrupt;
            seen_rounds = true;
        } else if(version >= 2 && key_is(line, kn, "Vibrate")) {
            if(seen_vib) return ItParseCorrupt;
            if(vn == 4 && memcmp(val, "true", 4) == 0) {
                tmp.vibrate = true;
            } else if(vn == 5 && memcmp(val, "false", 5) == 0) {
                tmp.vibrate = false;
            } else {
                return ItParseCorrupt;
            }
            seen_vib = true;
        }
        /* other keys: ignored (forward-tolerant within a known version) */
        line_no++;
    }

    if(line_no < 2 || !seen_work || !seen_rest) return ItParseCorrupt;
    if(version >= 2 && (!seen_rounds || !seen_vib)) return ItParseCorrupt;

    *out = tmp;
    return version == IT_VERSION_CURRENT ? ItParseOk : ItParseMigrated;
}

size_t it_settings_format(const ItSettings* s, char* buf, size_t cap) {
    int n = snprintf(
        buf,
        cap,
        "Filetype: %s\nVersion: %d\nWork: %lu\nRest: %lu\nRounds: %lu\nVibrate: %s\n",
        IT_FILETYPE,
        IT_VERSION_CURRENT,
        (unsigned long)s->work,
        (unsigned long)s->rest,
        (unsigned long)s->rounds,
        s->vibrate ? "true" : "false");
    if(n <= 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}
