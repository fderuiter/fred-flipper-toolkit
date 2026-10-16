// Host test for the task 02 reference settings logic.
// Build: cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined settings_store.c test_settings_store.c
// Run:   ./a.out ../../fixtures/02-settings/samples
#include "settings_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- modeled storage with failure injection ---- */
#define MAXF 4
typedef struct {
    char path[64];
    char data[SETTINGS_MAX_BYTES + 64];
    size_t len;
    bool used;
} MemFile;
typedef struct {
    MemFile f[MAXF];
    int ops;
    int fail_at; /* fail (crash) when ops == fail_at */
    bool crashed;
} MemFs;

static MemFile* find(MemFs* m, const char* path) {
    for(int i = 0; i < MAXF; i++)
        if(m->f[i].used && strcmp(m->f[i].path, path) == 0) return &m->f[i];
    return NULL;
}
static bool tick(MemFs* m) { /* returns false if this mutating op "crashes" */
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
    if(!f)
        for(int i = 0; i < MAXF && !f; i++)
            if(!m->f[i].used) {
                f = &m->f[i];
                f->used = true;
                snprintf(f->path, sizeof f->path, "%s", p);
            }
    if(!f) return false;
    if(!tick(m)) { /* crash mid-write: half the bytes land */
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

static const SettingsPaths P = {"/data/settings.txt", "/data/settings.tmp", "/data/settings.bak"};

static int failures;

/* Field-wise comparison: memcmp would also compare indeterminate padding bytes. */
static bool same(const Settings* a, const Settings* b) {
    return a->work_s == b->work_s && a->rest_s == b->rest_s && a->rounds == b->rounds &&
           a->vibrate == b->vibrate;
}
#define CHECK(cond, msg)              \
    do {                              \
        if(cond)                      \
            printf("PASS %s\n", msg); \
        else {                        \
            printf("FAIL %s\n", msg); \
            failures++;               \
        }                             \
    } while(0)

static SettingsStatus parse_file(const char* dir, const char* name, Settings* s) {
    char path[512], buf[SETTINGS_MAX_BYTES + 2];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("cannot read %s\n", path);
        exit(2);
    }
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return settings_parse(buf, n, s);
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    Settings s;

    SettingsStatus st = parse_file(dir, "settings_v1.txt", &s);
    CHECK(
        st == SettingsMigrated && s.work_s == 45 && s.rest_s == 15 && s.rounds == 8 && s.vibrate,
        "v1 migrates, keeps Work/Rest, defaults Rounds/Vibrate");
    st = parse_file(dir, "settings_v2.txt", &s);
    CHECK(
        st == SettingsLoaded && s.work_s == 30 && s.rest_s == 10 && s.rounds == 8 && s.vibrate,
        "v2 loads");
    st = parse_file(dir, "settings_v2_corrupt.txt", &s);
    CHECK(st == SettingsCorrupt && s.work_s == 30 && s.rounds == 8, "corrupt v2 -> defaults");
    st = parse_file(dir, "settings_v7_future.txt", &s);
    CHECK(st == SettingsFutureVersion, "future version detected");

    /* Round trip */
    Settings w = {120, 0, 99, false}, r;
    char buf[SETTINGS_MAX_BYTES];
    size_t n = settings_serialize(&w, buf, sizeof buf);
    CHECK(
        n > 0 && settings_parse(buf, n, &r) == SettingsLoaded && same(&w, &r),
        "serialize/parse round trip");
    Settings bad = {4, 0, 1, true};
    MemFs m0 = {.fail_at = -1};
    SettingsFs fs0 = {m_read, m_write, m_rename, m_remove, m_exists, &m0};
    CHECK(!settings_save(&fs0, &P, &bad), "out-of-range settings are not saved");

    /* Future file is never overwritten by load. */
    {
        MemFs m = {.fail_at = -1};
        SettingsFs fs = {m_read, m_write, m_rename, m_remove, m_exists, &m};
        const char* fut = "Filetype: Interval Timer Settings\nVersion: 7\nWork: 30\n";
        m_write(&m, P.path, fut, strlen(fut));
        st = settings_load(&fs, &P, &s);
        MemFile* f = find(&m, P.path);
        CHECK(
            st == SettingsFutureVersion && f && f->len == strlen(fut) &&
                memcmp(f->data, fut, f->len) == 0,
            "future file untouched by load");
    }

    /* Interrupted save at every mutating step: afterwards load yields either the
     * old or the new settings, never a corrupt state, and never crashes. */
    Settings old = {45, 15, 8, true}, neu = {200, 20, 3, false};
    int steps_max = 0;
    {
        MemFs m = {.fail_at = -1};
        SettingsFs fs = {m_read, m_write, m_rename, m_remove, m_exists, &m};
        char b[SETTINGS_MAX_BYTES];
        size_t k = settings_serialize(&old, b, sizeof b);
        m_write(&m, P.path, b, k);
        settings_save(&fs, &P, &old);
        m.ops = 0;
        settings_save(&fs, &P, &neu);
        steps_max = m.ops;
    }
    for(int at = 1; at <= steps_max + 1; at++) {
        MemFs m = {.fail_at = -1};
        SettingsFs fs = {m_read, m_write, m_rename, m_remove, m_exists, &m};
        char b[SETTINGS_MAX_BYTES];
        size_t k = settings_serialize(&old, b, sizeof b);
        m_write(&m, P.path, b, k);
        settings_save(&fs, &P, &old); /* creates a bak too */
        m.ops = 0;
        m.fail_at = at;
        m.crashed = false;
        bool saved = settings_save(&fs, &P, &neu);
        m.fail_at = -1;
        m.crashed = false; /* "reboot" */
        st = settings_load(&fs, &P, &s);
        bool is_old = same(&s, &old), is_new = same(&s, &neu);
        char label[96];
        snprintf(
            label,
            sizeof label,
            "crash at step %d/%d: saved=%d -> %s",
            at,
            steps_max,
            saved,
            is_new ? "new" :
            is_old ? "old" :
                     "OTHER");
        CHECK(
            (is_old || is_new) && (st == SettingsLoaded || st == SettingsMigrated) &&
                (!saved || is_new),
            label);
    }
    /* Missing file -> defaults */
    {
        MemFs m = {.fail_at = -1};
        SettingsFs fs = {m_read, m_write, m_rename, m_remove, m_exists, &m};
        st = settings_load(&fs, &P, &s);
        CHECK(st == SettingsMissing && s.work_s == 30, "missing file -> defaults");
    }

    /* Byte-flip fuzz of a valid file: parse never crashes; result is valid or defaults. */
    {
        char b[SETTINGS_MAX_BYTES];
        size_t k = settings_serialize(&neu, b, sizeof b);
        int bad_accepts = 0;
        for(size_t i = 0; i < k; i++)
            for(int bit = 0; bit < 8; bit++) {
                char c[SETTINGS_MAX_BYTES];
                memcpy(c, b, k);
                c[i] ^= (char)(1 << bit);
                Settings x;
                SettingsStatus t = settings_parse(c, k, &x);
                if((t == SettingsLoaded || t == SettingsMigrated) && !settings_valid(&x))
                    bad_accepts++;
            }
        CHECK(bad_accepts == 0, "bit-flip fuzz: no invalid settings accepted");
    }
    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
