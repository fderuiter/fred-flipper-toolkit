#include "interval_logic.h"

#include <string.h>
#include <stdio.h>

#define FILETYPE "Interval Timer Settings"

void it_settings_defaults(ItSettings* s) {
    s->work_s = IT_WORK_DEFAULT;
    s->rest_s = IT_REST_DEFAULT;
    s->rounds = IT_ROUNDS_DEFAULT;
    s->vibrate = true;
}

static bool parse_uint(const char* s, size_t n, uint32_t max, uint32_t* out) {
    if(n == 0 || n > 9) return false; /* 9 digits cannot overflow uint32_t */
    uint32_t v = 0;
    for(size_t i = 0; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        v = v * 10u + (uint32_t)(s[i] - '0');
    }
    if(v > max) return false;
    *out = v;
    return true;
}

static bool parse_bool(const char* s, size_t n, bool* out) {
    if(n == 4 && memcmp(s, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if(n == 5 && memcmp(s, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}

enum {
    SeenWork = 1 << 0,
    SeenRest = 1 << 1,
    SeenRounds = 1 << 2,
    SeenVibrate = 1 << 3,
};

typedef struct {
    ItSettings v; /* values that parsed and passed range checks */
    unsigned seen; /* keys seen (valid or not) */
    unsigned valid; /* keys seen with a valid value */
    bool duplicate;
} Fields;

static void field_apply(Fields* f, const char* key, size_t klen, const char* val, size_t vlen) {
    uint32_t n = 0;
    bool b = false;
    unsigned bit = 0;
    bool ok = false;

    if(klen == 4 && memcmp(key, "Work", 4) == 0) {
        bit = SeenWork;
        ok = parse_uint(val, vlen, IT_WORK_MAX, &n) && n >= IT_WORK_MIN;
        if(ok) f->v.work_s = (uint16_t)n;
    } else if(klen == 4 && memcmp(key, "Rest", 4) == 0) {
        bit = SeenRest;
        ok = parse_uint(val, vlen, IT_REST_MAX, &n);
        if(ok) f->v.rest_s = (uint16_t)n;
    } else if(klen == 6 && memcmp(key, "Rounds", 6) == 0) {
        bit = SeenRounds;
        ok = parse_uint(val, vlen, IT_ROUNDS_MAX, &n) && n >= IT_ROUNDS_MIN;
        if(ok) f->v.rounds = (uint8_t)n;
    } else if(klen == 7 && memcmp(key, "Vibrate", 7) == 0) {
        bit = SeenVibrate;
        ok = parse_bool(val, vlen, &b);
        if(ok) f->v.vibrate = b;
    } else {
        return; /* unknown key: ignored (forward compatibility) */
    }
    if(f->seen & bit) f->duplicate = true;
    f->seen |= bit;
    if(ok) f->valid |= bit;
}

ItParseResult it_settings_parse(const char* text, size_t len, ItSettings* out) {
    it_settings_defaults(out);
    if(text == NULL || len == 0 || len > IT_FILE_MAX_SIZE) return ItParseInvalid;

    for(size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if(c == '\n' || c == '\r') continue;
        if(c < 0x20 || c > 0x7e) return ItParseInvalid; /* NUL, control, non-ASCII */
    }

    Fields f;
    it_settings_defaults(&f.v);
    f.seen = f.valid = 0;
    f.duplicate = false;

    int content_line = 0;
    uint32_t version = 0;
    size_t pos = 0;

    while(pos < len) {
        size_t end = pos;
        while(end < len && text[end] != '\n') end++;
        size_t line_len = end - pos;
        const char* line = text + pos;
        pos = end + 1;
        if(line_len > 0 && line[line_len - 1] == '\r') line_len--;
        if(line_len == 0) continue;
        if(line_len > IT_LINE_MAX) return ItParseInvalid;

        const char* colon = memchr(line, ':', line_len);
        if(colon == NULL) return ItParseInvalid; /* includes truncated final line */
        size_t klen = (size_t)(colon - line);
        const char* val = colon + 1;
        size_t vlen = line_len - klen - 1;
        while(vlen > 0 && *val == ' ') {
            val++;
            vlen--;
        }

        if(content_line == 0) {
            if(klen != 8 || memcmp(line, "Filetype", 8) != 0) return ItParseInvalid;
            if(vlen != strlen(FILETYPE) || memcmp(val, FILETYPE, vlen) != 0)
                return ItParseInvalid;
        } else if(content_line == 1) {
            if(klen != 7 || memcmp(line, "Version", 7) != 0) return ItParseInvalid;
            if(!parse_uint(val, vlen, 1000000u, &version) || version < 1) return ItParseInvalid;
        } else {
            field_apply(&f, line, klen, val, vlen);
        }
        content_line++;
    }

    if(content_line < 2) return ItParseInvalid;

    if(version > IT_FORMAT_VERSION) {
        /* Best effort: keep whatever known fields are valid, default the rest. */
        *out = f.v;
        for(unsigned bit = SeenWork; bit <= SeenVibrate; bit <<= 1) {
            if(!(f.valid & bit)) {
                ItSettings d;
                it_settings_defaults(&d);
                if(bit == SeenWork) out->work_s = d.work_s;
                if(bit == SeenRest) out->rest_s = d.rest_s;
                if(bit == SeenRounds) out->rounds = d.rounds;
                if(bit == SeenVibrate) out->vibrate = d.vibrate;
            }
        }
        return ItParseFuture;
    }

    /* Known versions: every required key present, valid and not duplicated; else all defaults. */
    unsigned required = (version == 1) ? (SeenWork | SeenRest) :
                                         (SeenWork | SeenRest | SeenRounds | SeenVibrate);
    if(f.duplicate) return ItParseInvalid;
    if((f.valid & required) != required) return ItParseInvalid;
    if((f.seen & ~f.valid) != 0) return ItParseInvalid; /* an optional key present but bad */

    *out = f.v; /* v1: Rounds/Vibrate stay at defaults from it_settings_defaults */
    return (version == 1) ? ItParseMigrated : ItParseOk;
}

size_t it_settings_serialize(const ItSettings* s, char* buf, size_t cap) {
    if(s->work_s < IT_WORK_MIN || s->work_s > IT_WORK_MAX || s->rest_s > IT_REST_MAX ||
       s->rounds < IT_ROUNDS_MIN || s->rounds > IT_ROUNDS_MAX)
        return 0;
    int n = snprintf(
        buf,
        cap,
        "Filetype: " FILETYPE "\nVersion: %d\nWork: %u\nRest: %u\nRounds: %u\nVibrate: %s\n",
        IT_FORMAT_VERSION,
        (unsigned)s->work_s,
        (unsigned)s->rest_s,
        (unsigned)s->rounds,
        s->vibrate ? "true" : "false");
    if(n <= 0 || (size_t)n >= cap) return 0;
    return (size_t)n;
}

void it_session_start(ItSession* s, const ItSettings* cfg) {
    s->phase = ItPhaseWork;
    s->round = 1;
    s->ms_left = (uint32_t)cfg->work_s * 1000u;
}

uint8_t it_session_step(ItSession* s, const ItSettings* cfg, uint32_t elapsed_ms) {
    uint8_t flags = 0;
    while(s->phase != ItPhaseDone) {
        if(elapsed_ms < s->ms_left) {
            s->ms_left -= elapsed_ms;
            break;
        }
        elapsed_ms -= s->ms_left;
        flags |= IT_STEP_PHASE_CHANGED;
        if(s->phase == ItPhaseWork) {
            if(s->round >= cfg->rounds) {
                s->phase = ItPhaseDone;
                s->ms_left = 0;
                flags |= IT_STEP_FINISHED;
            } else if(cfg->rest_s > 0) {
                s->phase = ItPhaseRest;
                s->ms_left = (uint32_t)cfg->rest_s * 1000u;
            } else {
                s->round++;
                s->ms_left = (uint32_t)cfg->work_s * 1000u;
            }
        } else { /* rest */
            s->round++;
            s->phase = ItPhaseWork;
            s->ms_left = (uint32_t)cfg->work_s * 1000u;
        }
    }
    return flags;
}
