// Host test for the H02 reference settings logic.
// Build: cc -std=c99 -Wall -Wextra -Werror pomodoro_settings.c test_pomodoro_settings.c
// Run:   ./a.out ../../fixtures/h02-pomodoro/samples
#include "pomodoro_settings.h"
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if(!(c)) {                                                                \
            failures++;                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while(0)

/* ---- modeled storage with crash injection ---- */
#define MAXF 4
typedef struct {
    char path[64];
    char data[POMO_MAX_BYTES];
    size_t len;
    bool used;
} MemFile;
typedef struct {
    MemFile f[MAXF];
    int ops, fail_at;
    bool crashed;
} MemFs;
static MemFile* find(MemFs* m, const char* p) {
    for(int i = 0; i < MAXF; i++)
        if(m->f[i].used && strcmp(m->f[i].path, p) == 0) return &m->f[i];
    return NULL;
}
static bool tick(MemFs* m) {
    if(m->crashed) return false;
    if(++m->ops == m->fail_at) {
        m->crashed = true;
        return false;
    }
    return true;
}
static bool m_read(void* c, const char* p, char* b, size_t cap, size_t* len) {
    MemFile* f = find(c, p);
    if(!f || f->len > cap) return false;
    memcpy(b, f->data, f->len);
    *len = f->len;
    return true;
}
static bool m_write(void* c, const char* p, const char* b, size_t len) {
    MemFs* m = c;
    MemFile* f = find(m, p);
    for(int i = 0; i < MAXF && !f; i++)
        if(!m->f[i].used) {
            f = &m->f[i];
            f->used = true;
            snprintf(f->path, sizeof f->path, "%s", p);
        }
    if(!f || len > sizeof f->data) return false;
    if(!tick(m)) {
        memcpy(f->data, b, len / 2);
        f->len = len / 2;
        return false;
    }
    memcpy(f->data, b, len);
    f->len = len;
    return true;
}
static bool m_rename(void* c, const char* from, const char* to) {
    MemFs* m = c;
    MemFile* f = find(m, from);
    if(!f || find(m, to) || !tick(m)) return false;
    snprintf(f->path, sizeof f->path, "%s", to);
    return true;
}
static bool m_remove(void* c, const char* p) {
    MemFs* m = c;
    MemFile* f = find(m, p);
    if(!f || !tick(m)) return false;
    f->used = false;
    return true;
}
static bool m_exists(void* c, const char* p) {
    return find(c, p) != NULL;
}
static void put(MemFs* m, const char* p, const char* b, size_t n) {
    int saved = m->fail_at;
    m->fail_at = 0;
    m_write(m, p, b, n);
    m->fail_at = saved;
}

static size_t slurp(const char* dir, const char* name, char* buf, size_t cap) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "cannot open %s\n", path);
        failures++;
        return 0;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static PomoStatus parse_file(const char* dir, const char* name, PomoSettings* s) {
    char buf[POMO_MAX_BYTES];
    size_t n = slurp(dir, name, buf, sizeof buf);
    return pomo_parse(buf, n, s);
}

static bool same(const PomoSettings* s, uint32_t w, uint32_t sb, uint32_t lb, uint32_t c, bool snd) {
    return s->work_min == w && s->short_break_min == sb && s->long_break_min == lb &&
           s->cycles == c && s->sound == snd;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "../../fixtures/h02-pomodoro/samples";
    PomoSettings s;

    CHECK(parse_file(dir, "settings_v1.txt", &s) == PomoMigrated);
    CHECK(same(&s, 50, 10, 15, 4, true)); /* rename trap: Break -> ShortBreak */
    CHECK(parse_file(dir, "settings_v2.txt", &s) == PomoMigrated);
    CHECK(same(&s, 25, 5, 20, 4, true));
    CHECK(parse_file(dir, "settings_v2_legacy_key.txt", &s) == PomoMigrated);
    CHECK(same(&s, 30, 7, 25, 4, true));
    CHECK(parse_file(dir, "settings_v3.txt", &s) == PomoLoaded);
    CHECK(same(&s, 40, 8, 30, 3, false));
    CHECK(parse_file(dir, "settings_v3_corrupt.txt", &s) == PomoCorrupt);
    CHECK(same(&s, 25, 5, 15, 4, true));
    CHECK(parse_file(dir, "settings_v3_truncated.txt", &s) == PomoCorrupt);
    CHECK(parse_file(dir, "settings_v9_future.txt", &s) == PomoFutureVersion);
    CHECK(same(&s, 25, 5, 15, 4, true));

    /* inline edge cases */
    const char* both = "Filetype: Pomodoro Settings\nVersion: 2\nWork: 5\nBreak: 3\nShortBreak: 4\nLongBreak: 9\n";
    CHECK(pomo_parse(both, strlen(both), &s) == PomoCorrupt);
    const char* v3break = "Filetype: Pomodoro Settings\nVersion: 3\nWork: 5\nBreak: 3\nShortBreak: 4\nLongBreak: 9\nCycles: 2\nSound: true\n";
    CHECK(pomo_parse(v3break, strlen(v3break), &s) == PomoCorrupt);
    CHECK(pomo_parse("", 0, &s) == PomoCorrupt);

    /* round trip */
    PomoSettings r = {.work_min = 90, .short_break_min = 30, .long_break_min = 60, .cycles = 12, .sound = false};
    char out[POMO_MAX_BYTES];
    size_t n = pomo_serialize(&r, out, sizeof out);
    CHECK(n > 0 && pomo_parse(out, n, &s) == PomoLoaded && same(&s, 90, 30, 60, 12, false));
    CHECK(pomo_serialize(&r, out, 10) == 0);

    PomoPaths p = {.path = "/s.txt", .tmp = "/s.tmp", .bak = "/s.bak"};

    /* future file is never overwritten */
    {
        MemFs m = {0};
        PomoFs fs = {m_read, m_write, m_rename, m_remove, m_exists, &m};
        char fut[POMO_MAX_BYTES];
        size_t fn = slurp(dir, "settings_v9_future.txt", fut, sizeof fut);
        put(&m, p.path, fut, fn);
        CHECK(pomo_load(&fs, &p, &s) == PomoFutureVersion);
        CHECK(!pomo_save(&fs, &p, &s));
        MemFile* f = find(&m, p.path);
        CHECK(f && f->len == fn && memcmp(f->data, fut, fn) == 0);
    }

    /* crash at every mutating step of a migration save: a valid copy survives */
    char v1[POMO_MAX_BYTES];
    size_t v1n = slurp(dir, "settings_v1.txt", v1, sizeof v1);
    for(int fail = 1; fail <= 6; fail++) {
        MemFs m = {0};
        PomoFs fs = {m_read, m_write, m_rename, m_remove, m_exists, &m};
        put(&m, p.path, v1, v1n);
        CHECK(pomo_load(&fs, &p, &s) == PomoMigrated);
        m.fail_at = fail;
        bool ok = pomo_save(&fs, &p, &s);
        m.fail_at = 0;
        m.crashed = false;
        PomoStatus st = pomo_load(&fs, &p, &s);
        CHECK(st == PomoLoaded || st == PomoMigrated);
        CHECK(same(&s, 50, 10, 15, 4, true));
        if(ok) CHECK(st == PomoLoaded);
        CHECK(!m_exists(&m, p.tmp));
    }

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
