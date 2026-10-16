// Reviewer-side reference solution for FRED held-out task H02. MIT licensed, original.
#include "pomodoro_settings.h"
#include <stdio.h>
#include <string.h>

void pomo_defaults(PomoSettings* s) {
    s->work_min = 25;
    s->short_break_min = 5;
    s->long_break_min = 15;
    s->cycles = 4;
    s->sound = true;
}

bool pomo_valid(const PomoSettings* s) {
    return s->work_min >= 1 && s->work_min <= 90 && s->short_break_min >= 1 &&
           s->short_break_min <= 30 && s->long_break_min >= 1 && s->long_break_min <= 60 &&
           s->cycles >= 1 && s->cycles <= 12;
}

/* Overflow-safe decimal parse; rejects signs, empty values and > max. */
static bool parse_u32(const char* v, size_t n, uint32_t max, uint32_t* out) {
    if(n == 0 || n > 10) return false;
    uint64_t acc = 0;
    for(size_t i = 0; i < n; i++) {
        if(v[i] < '0' || v[i] > '9') return false;
        acc = acc * 10u + (uint64_t)(v[i] - '0');
        if(acc > max) return false;
    }
    *out = (uint32_t)acc;
    return true;
}

static bool eq(const char* a, size_t n, const char* lit) {
    return strlen(lit) == n && memcmp(a, lit, n) == 0;
}

enum { K_WORK = 1, K_BREAK = 2, K_SHORT = 4, K_LONG = 8, K_CYCLES = 16, K_SOUND = 32 };

PomoStatus pomo_parse(const char* buf, size_t len, PomoSettings* out) {
    PomoSettings s;
    pomo_defaults(&s);
    pomo_defaults(out);
    uint32_t version = 0, seen = 0, legacy_break = 0;
    bool have_type = false, have_version = false;
    size_t pos = 0;
    unsigned line_no = 0;
    while(pos < len) {
        size_t end = pos;
        while(end < len && buf[end] != '\n') end++;
        if(end == len && (len == 0 || buf[len - 1] != '\n')) return PomoCorrupt; /* truncated */
        size_t n = end - pos;
        const char* line = buf + pos;
        pos = end + 1;
        line_no++;
        if(n > 0 && line[n - 1] == '\r') n--;
        if(n == 0 || line[0] == '#') continue;
        const char* colon = memchr(line, ':', n);
        if(!colon || colon + 1 >= line + n || colon[1] != ' ') return PomoCorrupt;
        size_t kn = (size_t)(colon - line);
        const char* v = colon + 2;
        size_t vn = n - kn - 2;
        if(line_no == 1) {
            if(!eq(line, kn, "Filetype") || !eq(v, vn, POMO_FILETYPE)) return PomoCorrupt;
            have_type = true;
            continue;
        }
        if(line_no == 2) {
            if(!eq(line, kn, "Version") || !parse_u32(v, vn, 1000000u, &version) || version == 0)
                return PomoCorrupt;
            have_version = true;
            if(version > POMO_VERSION) return PomoFutureVersion;
            continue;
        }
        uint32_t bit = 0, *dst = NULL, max = 0;
        if(eq(line, kn, "Work")) bit = K_WORK, dst = &s.work_min, max = 90;
        else if(eq(line, kn, "Break") && version <= 2) bit = K_BREAK, dst = &legacy_break, max = 30;
        else if(eq(line, kn, "ShortBreak") && version >= 2) bit = K_SHORT, dst = &s.short_break_min, max = 30;
        else if(eq(line, kn, "LongBreak") && version >= 2) bit = K_LONG, dst = &s.long_break_min, max = 60;
        else if(eq(line, kn, "Cycles") && version >= 3) bit = K_CYCLES, dst = &s.cycles, max = 12;
        else if(eq(line, kn, "Sound") && version >= 3) bit = K_SOUND;
        else return PomoCorrupt; /* unknown key for this version */
        if(seen & bit) return PomoCorrupt; /* duplicate */
        seen |= bit;
        if(bit == K_SOUND) {
            if(eq(v, vn, "true")) s.sound = true;
            else if(eq(v, vn, "false")) s.sound = false;
            else return PomoCorrupt;
        } else if(!parse_u32(v, vn, max, dst)) {
            return PomoCorrupt;
        }
    }
    if(!have_type || !have_version) return PomoCorrupt;
    /* Required keys per version; v1 Break and v2 legacy Break map to ShortBreak. */
    uint32_t need = version == 1 ? (K_WORK | K_BREAK) :
                    version == 2 ? (K_WORK | K_LONG) :
                                   (K_WORK | K_SHORT | K_LONG | K_CYCLES | K_SOUND);
    if((seen & need) != need) return PomoCorrupt;
    if(version == 2) {
        bool has_short = seen & K_SHORT, has_break = seen & K_BREAK;
        if(has_short == has_break) return PomoCorrupt; /* neither or both */
    }
    if(seen & K_BREAK) s.short_break_min = legacy_break;
    if(!pomo_valid(&s)) return PomoCorrupt;
    *out = s;
    return version == POMO_VERSION ? PomoLoaded : PomoMigrated;
}

size_t pomo_serialize(const PomoSettings* s, char* buf, size_t cap) {
    int n = snprintf(
        buf,
        cap,
        "Filetype: %s\nVersion: %u\nWork: %lu\nShortBreak: %lu\nLongBreak: %lu\nCycles: %lu\nSound: %s\n",
        POMO_FILETYPE,
        POMO_VERSION,
        (unsigned long)s->work_min,
        (unsigned long)s->short_break_min,
        (unsigned long)s->long_break_min,
        (unsigned long)s->cycles,
        s->sound ? "true" : "false");
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

static PomoStatus read_status(const PomoFs* fs, const char* path, PomoSettings* out) {
    char buf[POMO_MAX_BYTES];
    size_t len = 0;
    if(!fs->read_all(fs->ctx, path, buf, sizeof buf, &len)) return PomoCorrupt;
    return pomo_parse(buf, len, out);
}

PomoStatus pomo_load(const PomoFs* fs, const PomoPaths* p, PomoSettings* out) {
    pomo_defaults(out);
    /* Recover an interrupted save: a verified tmp wins only if current is gone. */
    if(fs->exists(fs->ctx, p->tmp)) {
        PomoSettings t;
        if(!fs->exists(fs->ctx, p->path) && read_status(fs, p->tmp, &t) == PomoLoaded) {
            fs->rename(fs->ctx, p->tmp, p->path);
        } else {
            fs->remove(fs->ctx, p->tmp);
        }
    }
    if(!fs->exists(fs->ctx, p->path)) {
        if(fs->exists(fs->ctx, p->bak) && read_status(fs, p->bak, out) == PomoLoaded) return PomoLoaded;
        pomo_defaults(out);
        return PomoMissing;
    }
    PomoStatus st = read_status(fs, p->path, out);
    if(st == PomoCorrupt || st == PomoFutureVersion) pomo_defaults(out);
    return st;
}

bool pomo_save(const PomoFs* fs, const PomoPaths* p, const PomoSettings* s) {
    if(!pomo_valid(s)) return false;
    PomoSettings probe;
    if(fs->exists(fs->ctx, p->path) && read_status(fs, p->path, &probe) == PomoFutureVersion)
        return false; /* never clobber a newer release's file */
    char buf[POMO_MAX_BYTES], check[POMO_MAX_BYTES];
    size_t n = pomo_serialize(s, buf, sizeof buf), m = 0;
    if(n == 0) return false;
    if(fs->exists(fs->ctx, p->tmp) && !fs->remove(fs->ctx, p->tmp)) return false;
    if(!fs->write_all(fs->ctx, p->tmp, buf, n)) return false;
    if(!fs->read_all(fs->ctx, p->tmp, check, sizeof check, &m) || m != n || memcmp(buf, check, n))
        return false;
    if(fs->exists(fs->ctx, p->path)) {
        if(fs->exists(fs->ctx, p->bak) && !fs->remove(fs->ctx, p->bak)) return false;
        if(!fs->rename(fs->ctx, p->path, p->bak)) return false;
    }
    return fs->rename(fs->ctx, p->tmp, p->path);
}
