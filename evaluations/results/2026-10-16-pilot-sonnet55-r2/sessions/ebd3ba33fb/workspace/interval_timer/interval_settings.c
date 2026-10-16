#include "interval_settings.h"

#include <stdio.h>
#include <string.h>

#define SWITCH_S 60 /* 1 s steps up to here, 5 s steps above */
#define STEP_S   5

void it_settings_defaults(ItSettings* s) {
    s->work_s = 30;
    s->rest_s = 10;
    s->rounds = 8;
    s->vibrate = true;
}

static bool key_is(const char* k, size_t klen, const char* name) {
    return klen == strlen(name) && memcmp(k, name, klen) == 0;
}

/* Optional '-', 1..9 digits, nothing else. */
static bool parse_int(const char* s, size_t n, long* out) {
    size_t i = 0;
    bool neg = false;
    if(n > 0 && s[0] == '-') {
        neg = true;
        i = 1;
    }
    size_t digits = n - i;
    if(digits < 1 || digits > 9) return false;
    long v = 0;
    for(; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    *out = neg ? -v : v;
    return true;
}

static long clamp_l(long v, long lo, long hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Range handling: v2 rejects out-of-range, v1 (older release, ranges unknown)
 * clamps so the user's value is kept as closely as possible. */
static bool take_range(long v, long lo, long hi, bool clamp, long* out) {
    if(v < lo || v > hi) {
        if(!clamp) return false;
        v = clamp_l(v, lo, hi);
    }
    *out = v;
    return true;
}

ItParseResult it_settings_parse(const char* buf, size_t len, ItSettings* out) {
    if(!buf || !out || len == 0 || len > IT_FILE_MAX) return ItParseCorrupt;

    ItSettings t;
    it_settings_defaults(&t);
    size_t pos = 0;
    unsigned line_no = 0;
    int version = 0;
    unsigned seen = 0;

    while(pos < len) {
        const char* line = buf + pos;
        const char* nl = memchr(line, '\n', len - pos);
        if(!nl) return ItParseCorrupt; /* truncated final line */
        size_t ll = (size_t)(nl - line);
        pos += ll + 1;
        if(ll > 0 && line[ll - 1] == '\r') ll--;
        if(++line_no > IT_LINES_MAX || ll > IT_LINE_MAX) return ItParseCorrupt;
        if(memchr(line, 0, ll)) return ItParseCorrupt;

        const char* colon = memchr(line, ':', ll);
        if(!colon) return ItParseCorrupt;
        size_t klen = (size_t)(colon - line);
        const char* val = colon + 1;
        size_t vlen = ll - klen - 1;
        if(vlen > 0 && val[0] == ' ') {
            val++;
            vlen--;
        }

        if(line_no == 1) {
            if(!key_is(line, klen, "Filetype") || vlen != strlen(IT_FILETYPE) ||
               memcmp(val, IT_FILETYPE, vlen) != 0)
                return ItParseCorrupt;
            continue;
        }
        if(line_no == 2) {
            long v;
            if(!key_is(line, klen, "Version") || !parse_int(val, vlen, &v) || v < 1)
                return ItParseCorrupt;
            if(v > IT_VERSION) return ItParseFuture; /* body not interpreted */
            version = (int)v;
            continue;
        }

        const bool v1 = (version == 1);
        long n;
        unsigned bit;
        if(key_is(line, klen, "Work")) {
            bit = 1;
            if(seen & bit) return ItParseCorrupt;
            if(!parse_int(val, vlen, &n) || !take_range(n, IT_WORK_MIN, IT_WORK_MAX, v1, &n))
                return ItParseCorrupt;
            t.work_s = (uint16_t)n;
        } else if(key_is(line, klen, "Rest")) {
            bit = 2;
            if(seen & bit) return ItParseCorrupt;
            if(!parse_int(val, vlen, &n) || !take_range(n, IT_REST_MIN, IT_REST_MAX, v1, &n))
                return ItParseCorrupt;
            t.rest_s = (uint16_t)n;
        } else if(!v1 && key_is(line, klen, "Rounds")) {
            bit = 4;
            if(seen & bit) return ItParseCorrupt;
            if(!parse_int(val, vlen, &n) || !take_range(n, IT_ROUNDS_MIN, IT_ROUNDS_MAX, false, &n))
                return ItParseCorrupt;
            t.rounds = (uint8_t)n;
        } else if(!v1 && key_is(line, klen, "Vibrate")) {
            bit = 8;
            if(seen & bit) return ItParseCorrupt;
            if(vlen == 4 && memcmp(val, "true", 4) == 0)
                t.vibrate = true;
            else if(vlen == 5 && memcmp(val, "false", 5) == 0)
                t.vibrate = false;
            else
                return ItParseCorrupt;
        } else {
            continue; /* unknown keys in a known version are ignored */
        }
        seen |= bit;
    }

    if(version == 0) return ItParseCorrupt;
    if(version == 1) {
        if((seen & 3u) != 3u) return ItParseCorrupt;
        *out = t; /* Rounds/Vibrate take defaults */
        return ItParseMigratedV1;
    }
    if(seen != 15u) return ItParseCorrupt;
    *out = t;
    return ItParseOk;
}

size_t it_settings_format(const ItSettings* s, char* buf, size_t cap) {
    int n = snprintf(
        buf,
        cap,
        "Filetype: " IT_FILETYPE "\nVersion: %d\nWork: %u\nRest: %u\nRounds: %u\nVibrate: %s\n",
        IT_VERSION,
        (unsigned)s->work_s,
        (unsigned)s->rest_s,
        (unsigned)s->rounds,
        s->vibrate ? "true" : "false");
    if(n <= 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

/* ---- index mapping ---- */

uint8_t it_secs_count(uint16_t min, uint16_t max) {
    return (uint8_t)((SWITCH_S - min + 1) + (max - SWITCH_S) / STEP_S);
}

uint16_t it_secs_from_index(uint8_t idx, uint16_t min, uint16_t max) {
    const unsigned low = SWITCH_S - min; /* last 1 s-step index */
    unsigned v = idx <= low ? min + idx : SWITCH_S + STEP_S * (idx - low);
    return (uint16_t)(v > max ? max : v);
}

uint8_t it_secs_to_index(uint16_t secs, uint16_t min, uint16_t max) {
    if(secs < min) secs = min;
    if(secs > max) secs = max;
    const unsigned low = SWITCH_S - min;
    if(secs <= SWITCH_S) return (uint8_t)(secs - min);
    return (uint8_t)(low + (secs - SWITCH_S + STEP_S / 2) / STEP_S);
}

/* ---- store ---- */

static bool join_path(char* dst, const char* base, const char* suffix) {
    int n = snprintf(dst, IT_PATH_MAX, "%s%s", base, suffix);
    return n > 0 && n < IT_PATH_MAX;
}

bool it_store_init(ItStore* st, const ItStoreOps* ops, void* ctx, const char* main_path) {
    memset(st, 0, sizeof(*st));
    st->ops = ops;
    st->ctx = ctx;
    return join_path(st->main_path, main_path, "") && join_path(st->tmp_path, main_path, ".tmp") &&
           join_path(st->bak_path, main_path, ".bak") &&
           join_path(st->bad_path, main_path, ".bad");
}

/* Classify one candidate file. */
typedef enum { CandMissing, CandIoError, CandGood, CandMigrated, CandFuture, CandBad } Cand;

static Cand read_candidate(ItStore* st, const char* path, ItSettings* out) {
    char buf[IT_FILE_MAX + 1]; /* +1 so oversize files are detected, not truncated */
    size_t len = 0;
    ItIo io = st->ops->read(st->ctx, path, buf, sizeof(buf), &len);
    if(io == ItIoNotFound) return CandMissing;
    if(io != ItIoOk) return CandIoError;
    switch(it_settings_parse(buf, len, out)) {
    case ItParseOk:
        return CandGood;
    case ItParseMigratedV1:
        return CandMigrated;
    case ItParseFuture:
        return CandFuture;
    default:
        return CandBad;
    }
}

ItLoadStatus it_store_load(ItStore* st, ItSettings* out) {
    it_settings_defaults(out);
    st->read_only = false;
    st->main_corrupt = false;

    ItSettings t;
    switch(read_candidate(st, st->main_path, &t)) {
    case CandGood:
        *out = t;
        return ItLoadOk;
    case CandMigrated:
        *out = t;
        return ItLoadMigrated;
    case CandFuture:
        st->read_only = true; /* keep the newer file untouched */
        return ItLoadFuture;
    case CandIoError:
        st->read_only = true; /* cannot tell what is on the card */
        return ItLoadIoError;
    case CandBad:
        st->main_corrupt = true;
        break;
    case CandMissing:
        break;
    }

    /* Main missing or corrupt: an interrupted save may have left .tmp (complete,
     * synced before the swap) or .bak (previous good copy). */
    const char* alt[2] = {st->tmp_path, st->bak_path};
    for(int i = 0; i < 2; i++) {
        Cand c = read_candidate(st, alt[i], &t);
        if(c == CandGood || c == CandMigrated) {
            *out = t;
            return ItLoadRecovered;
        }
    }
    return st->main_corrupt ? ItLoadCorrupt : ItLoadFresh;
}

bool it_store_save(ItStore* st, const ItSettings* s) {
    if(st->read_only) return false;

    char buf[IT_FILE_MAX];
    size_t len = it_settings_format(s, buf, sizeof(buf));
    if(len == 0) return false;

    /* 1. Complete new copy first; main stays untouched if this fails. */
    if(st->ops->write(st->ctx, st->tmp_path, buf, len) != ItIoOk) {
        st->ops->remove(st->ctx, st->tmp_path);
        return false;
    }
    /* 2. Rotate main out of the way (a corrupt file is kept as .bad, never lost). */
    const char* rot = st->main_corrupt ? st->bad_path : st->bak_path;
    ItIo io = st->ops->remove(st->ctx, rot);
    if(io == ItIoError) return false;
    io = st->ops->rename(st->ctx, st->main_path, rot);
    if(io == ItIoError) return false;
    /* 3. Install the new copy. If this fails the load path recovers from .tmp. */
    if(st->ops->rename(st->ctx, st->tmp_path, st->main_path) != ItIoOk) return false;
    st->main_corrupt = false;
    return true;
}

/* ---- run state machine ---- */

void it_run_start(ItRun* r, const ItSettings* s) {
    r->phase = ItPhaseWork;
    r->round = 1;
    r->remaining_ms = (uint32_t)s->work_s * 1000u;
}

ItEvt it_run_advance(ItRun* r, const ItSettings* s, uint32_t elapsed_ms) {
    ItEvt evt = ItEvtNone;
    while(r->phase != ItPhaseDone && elapsed_ms >= r->remaining_ms) {
        elapsed_ms -= r->remaining_ms;
        if(r->phase == ItPhaseWork) {
            if(r->round >= s->rounds) {
                r->phase = ItPhaseDone;
                r->remaining_ms = 0;
                return ItEvtFinished;
            }
            if(s->rest_s > 0) {
                r->phase = ItPhaseRest;
                r->remaining_ms = (uint32_t)s->rest_s * 1000u;
                evt = ItEvtPhaseChanged;
                continue;
            }
        }
        /* rest finished, or work finished with no rest */
        r->phase = ItPhaseWork;
        r->round++;
        r->remaining_ms = (uint32_t)s->work_s * 1000u;
        evt = ItEvtPhaseChanged;
    }
    if(r->phase != ItPhaseDone) r->remaining_ms -= elapsed_ms;
    return evt;
}
