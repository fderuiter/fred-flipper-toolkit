#include "settings_logic.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    bool ok;
    uint32_t value;
} Field;

void it_settings_defaults(ITSettings* s) {
    s->work = IT_WORK_DEFAULT;
    s->rest = IT_REST_DEFAULT;
    s->rounds = IT_ROUNDS_DEFAULT;
    s->vibrate = IT_VIBRATE_DEFAULT;
}

bool it_load_status_is_readonly(ITLoadStatus st) {
    return st == ITLoadFuture || st == ITLoadTooLarge;
}

/* Digits only, 1..5 of them: no sign, no wrap. */
static bool parse_uint(const char* s, size_t n, uint32_t* out) {
    if(n == 0 || n > 5) return false;
    uint32_t v = 0;
    for(size_t i = 0; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    *out = v;
    return true;
}

/* Next '\n'-separated line, trailing '\r' stripped. */
static bool next_line(const char* buf, size_t len, size_t* pos, const char** line, size_t* n) {
    if(*pos >= len) return false;
    size_t start = *pos;
    size_t end = start;
    while(end < len && buf[end] != '\n')
        end++;
    *pos = (end < len) ? end + 1 : end;
    if(end > start && buf[end - 1] == '\r') end--;
    *line = buf + start;
    *n = end - start;
    return true;
}

/* Splits "Key: value". Value is trimmed. */
static bool split_kv(const char* line, size_t n, size_t* klen, const char** val, size_t* vlen) {
    size_t c = 0;
    while(c < n && line[c] != ':')
        c++;
    if(c == n || c == 0) return false;
    size_t vs = c + 1;
    while(vs < n && line[vs] == ' ')
        vs++;
    size_t ve = n;
    while(ve > vs && line[ve - 1] == ' ')
        ve--;
    *klen = c;
    *val = line + vs;
    *vlen = ve - vs;
    return true;
}

static bool key_is(const char* line, size_t klen, const char* key) {
    return strlen(key) == klen && memcmp(line, key, klen) == 0;
}

static void set_range(Field* f, const char* v, size_t n, uint32_t lo, uint32_t hi) {
    uint32_t x;
    f->ok = parse_uint(v, n, &x) && x >= lo && x <= hi;
    f->value = f->ok ? x : 0;
}

ITLoadStatus it_settings_parse(const char* buf, size_t len, ITSettings* out) {
    it_settings_defaults(out);
    if(len > IT_SETTINGS_MAX_BYTES) return ITLoadTooLarge;

    size_t pos = 0;
    const char* line;
    size_t n;

    /* Header: Filetype, then Version. */
    if(!next_line(buf, len, &pos, &line, &n)) return ITLoadCorrupt;
    if(n != strlen("Filetype: " IT_FILETYPE) || memcmp(line, "Filetype: " IT_FILETYPE, n) != 0)
        return ITLoadCorrupt;

    size_t klen;
    const char* val;
    size_t vlen;
    uint32_t version;
    if(!next_line(buf, len, &pos, &line, &n)) return ITLoadCorrupt;
    if(n > IT_SETTINGS_MAX_LINE || !split_kv(line, n, &klen, &val, &vlen) ||
       !key_is(line, klen, "Version") || !parse_uint(val, vlen, &version) || version < 1)
        return ITLoadCorrupt;

    Field work = {0}, rest = {0}, rounds = {0}, vib = {0};
    while(next_line(buf, len, &pos, &line, &n)) {
        if(n > IT_SETTINGS_MAX_LINE) continue;
        if(!split_kv(line, n, &klen, &val, &vlen)) continue; /* e.g. truncated "Vib" */
        if(key_is(line, klen, "Work")) {
            set_range(&work, val, vlen, IT_WORK_MIN, IT_WORK_MAX);
        } else if(key_is(line, klen, "Rest")) {
            set_range(&rest, val, vlen, IT_REST_MIN, IT_REST_MAX);
        } else if(version >= 2 && key_is(line, klen, "Rounds")) {
            set_range(&rounds, val, vlen, IT_ROUNDS_MIN, IT_ROUNDS_MAX);
        } else if(version >= 2 && key_is(line, klen, "Vibrate")) {
            if(vlen == 4 && memcmp(val, "true", 4) == 0) {
                vib.ok = true;
                vib.value = 1;
            } else if(vlen == 5 && memcmp(val, "false", 5) == 0) {
                vib.ok = true;
                vib.value = 0;
            } else {
                vib.ok = false;
            }
        } /* unknown keys ignored */
    }

    if(work.ok) out->work = (uint16_t)work.value;
    if(rest.ok) out->rest = (uint16_t)rest.value;
    if(version >= 2) {
        if(rounds.ok) out->rounds = (uint8_t)rounds.value;
        if(vib.ok) out->vibrate = vib.value != 0;
    }

    if(version > IT_FORMAT_VERSION) return ITLoadFuture;
    if(version == 1) return (work.ok && rest.ok) ? ITLoadMigratedV1 : ITLoadRepaired;
    return (work.ok && rest.ok && rounds.ok && vib.ok) ? ITLoadOk : ITLoadRepaired;
}

size_t it_settings_format(const ITSettings* s, char* out, size_t cap) {
    if(s->work < IT_WORK_MIN || s->work > IT_WORK_MAX || s->rest > IT_REST_MAX ||
       s->rounds < IT_ROUNDS_MIN || s->rounds > IT_ROUNDS_MAX)
        return 0;
    int r = snprintf(
        out,
        cap,
        "Filetype: " IT_FILETYPE "\nVersion: %d\nWork: %u\nRest: %u\nRounds: %u\nVibrate: %s\n",
        IT_FORMAT_VERSION,
        (unsigned)s->work,
        (unsigned)s->rest,
        (unsigned)s->rounds,
        s->vibrate ? "true" : "false");
    if(r <= 0 || (size_t)r >= cap) return 0;
    return (size_t)r;
}
