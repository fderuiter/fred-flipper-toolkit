#include "it_logic.h"

#include <stdio.h>
#include <string.h>

#define IT_MAX_LINE 64

void it_settings_defaults(ItSettings* s) {
    s->work = 30;
    s->rest = 10;
    s->rounds = 8;
    s->vibrate = true;
}

/* Strict decimal parse: digits only (optional leading '-' rejected by range), no wrap. */
static bool parse_uint(const char* p, long min, long max, long* out) {
    if(*p == '\0') return false;
    long v = 0;
    for(; *p; p++) {
        if(*p < '0' || *p > '9') return false;
        v = v * 10 + (*p - '0');
        if(v > max) return false; /* stops overflow too */
    }
    if(v < min) return false;
    *out = v;
    return true;
}

static bool parse_bool(const char* p, bool* out) {
    if(strcmp(p, "true") == 0) {
        *out = true;
        return true;
    }
    if(strcmp(p, "false") == 0) {
        *out = false;
        return true;
    }
    return false;
}

ItLoadResult it_settings_parse(const char* data, size_t len, ItSettings* s) {
    it_settings_defaults(s);
    if(data == NULL || len == 0 || len > IT_FILE_MAX_SIZE) return ItLoadBadFile;

    bool have_type = false;
    long version = -1;
    bool got_work = false, got_rest = false, got_rounds = false, got_vib = false;
    bool bad_field = false;
    ItSettings parsed = *s;

    size_t pos = 0;
    while(pos < len) {
        char line[IT_MAX_LINE];
        size_t n = 0;
        bool overlong = false;
        while(pos < len && data[pos] != '\n') {
            if(n < IT_MAX_LINE - 1)
                line[n++] = data[pos];
            else
                overlong = true;
            pos++;
        }
        pos++; /* skip '\n' */
        while(n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) n--;
        line[n] = '\0';
        if(n == 0) continue;
        if(overlong) {
            bad_field = true;
            continue;
        }

        char* colon = strchr(line, ':');
        if(colon == NULL) {
            bad_field = true; /* e.g. truncated final line */
            continue;
        }
        *colon = '\0';
        const char* key = line;
        const char* val = colon + 1;
        while(*val == ' ') val++;

        long v;
        if(strcmp(key, "Filetype") == 0) {
            have_type = strcmp(val, "Interval Timer Settings") == 0;
            if(!have_type) return ItLoadBadFile;
        } else if(strcmp(key, "Version") == 0) {
            if(!parse_uint(val, 0, 1000, &v)) return ItLoadBadFile;
            version = v;
        } else if(version < 0) {
            return ItLoadBadFile; /* header must come first */
        } else if(strcmp(key, "Work") == 0) {
            if(parse_uint(val, IT_WORK_MIN, IT_WORK_MAX, &v)) {
                parsed.work = (uint16_t)v;
                got_work = true;
            } else
                bad_field = true;
        } else if(strcmp(key, "Rest") == 0) {
            if(parse_uint(val, IT_REST_MIN, IT_REST_MAX, &v)) {
                parsed.rest = (uint16_t)v;
                got_rest = true;
            } else
                bad_field = true;
        } else if(version >= 2 && strcmp(key, "Rounds") == 0) {
            if(parse_uint(val, IT_ROUNDS_MIN, IT_ROUNDS_MAX, &v)) {
                parsed.rounds = (uint8_t)v;
                got_rounds = true;
            } else
                bad_field = true;
        } else if(version >= 2 && strcmp(key, "Vibrate") == 0) {
            if(parse_bool(val, &parsed.vibrate))
                got_vib = true;
            else
                bad_field = true;
        }
        /* unknown keys: ignored (last-wins for duplicates) */
    }

    if(!have_type || version < 1) return ItLoadBadFile;
    if(version > IT_FORMAT_VERSION) return ItLoadFutureVersion; /* s stays defaults */

    /* Valid fields kept; invalid or missing fields stay at defaults (per-field fallback). */
    if(got_work) s->work = parsed.work;
    if(got_rest) s->rest = parsed.rest;
    if(got_rounds) s->rounds = parsed.rounds;
    if(got_vib) s->vibrate = parsed.vibrate;

    if(version == 1) return bad_field ? ItLoadRepaired : ItLoadMigrated;
    bool complete = got_work && got_rest && got_rounds && got_vib;
    return (bad_field || !complete) ? ItLoadRepaired : ItLoadOk;
}

size_t it_settings_format(const ItSettings* s, char* buf, size_t cap) {
    int n = snprintf(
        buf,
        cap,
        "Filetype: Interval Timer Settings\nVersion: %d\nWork: %u\nRest: %u\nRounds: %u\nVibrate: %s\n",
        IT_FORMAT_VERSION,
        (unsigned)s->work,
        (unsigned)s->rest,
        (unsigned)s->rounds,
        s->vibrate ? "true" : "false");
    if(n < 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

void it_timer_start(ItTimer* t, const ItSettings* cfg) {
    t->cfg = *cfg;
    t->phase = ItPhaseWork;
    t->remaining = cfg->work;
    t->round = 1;
}

ItEvent it_timer_step(ItTimer* t) {
    if(t->phase == ItPhaseDone) return ItEventNone;
    if(t->remaining > 0) t->remaining--;
    if(t->remaining > 0) return ItEventNone;

    if(t->phase == ItPhaseWork) {
        if(t->round >= t->cfg.rounds) {
            t->phase = ItPhaseDone;
            return ItEventDone;
        }
        if(t->cfg.rest == 0) { /* no rest: straight into next work */
            t->round++;
            t->remaining = t->cfg.work;
        } else {
            t->phase = ItPhaseRest;
            t->remaining = t->cfg.rest;
        }
        return ItEventPhaseChange;
    }
    /* rest finished */
    t->round++;
    t->phase = ItPhaseWork;
    t->remaining = t->cfg.work;
    return ItEventPhaseChange;
}
