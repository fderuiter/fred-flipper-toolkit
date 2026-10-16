/* Host tests for the codec and store (no Furi). Not run in the authoring session.
 *
 *   cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I.. \
 *      test_settings.c ../it_settings.c ../it_store.c -o /tmp/it_test && /tmp/it_test ../../samples
 */
#include "it_settings.h"
#include "it_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) \
    do { \
        if(!(c)) { \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
            failures++; \
        } \
    } while(0)

static size_t read_sample(const char* dir, const char* name, char* buf, size_t cap) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("FAIL cannot open %s\n", path);
        failures++;
        return 0;
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

static void test_samples(const char* dir) {
    char buf[2048];
    ItSettings s;
    size_t n;

    n = read_sample(dir, "settings_v1.txt", buf, sizeof(buf));
    CHECK(it_settings_parse(buf, n, &s) == ItParseMigrated);
    CHECK(s.work == 45 && s.rest == 15 && s.rounds == 8 && s.vibrate); /* user values kept */

    n = read_sample(dir, "settings_v2.txt", buf, sizeof(buf));
    CHECK(it_settings_parse(buf, n, &s) == ItParseOk);
    CHECK(s.work == 30 && s.rest == 10 && s.rounds == 8 && s.vibrate);

    ItSettings untouched = {1, 2, 3, false};
    s = untouched;
    n = read_sample(dir, "settings_v2_corrupt.txt", buf, sizeof(buf));
    CHECK(it_settings_parse(buf, n, &s) == ItParseCorrupt);
    CHECK(memcmp(&s, &untouched, sizeof(s)) == 0);

    n = read_sample(dir, "settings_v7_future.txt", buf, sizeof(buf));
    CHECK(it_settings_parse(buf, n, &s) == ItParseFuture);
}

static ItParseResult p(const char* text) {
    ItSettings s;
    return it_settings_parse(text, strlen(text), &s);
}

static void test_edges(void) {
    const char* h = "Filetype: Interval Timer Settings\nVersion: 2\n";
    char b[256];
#define T(body, expect) \
    snprintf(b, sizeof(b), "%s%s", h, body); \
    CHECK(p(b) == (expect))
    T("Work: 5\nRest: 0\nRounds: 1\nVibrate: false\n", ItParseOk);
    T("Work: 600\nRest: 600\nRounds: 99\nVibrate: true\n", ItParseOk);
    T("Work: 4\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Work: 601\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Work: 10\nRest: 601\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Work: 10\nRest: 0\nRounds: 100\nVibrate: true\n", ItParseCorrupt);
    T("Work: 10\nRest: 0\nRounds: 0\nVibrate: true\n", ItParseCorrupt);
    T("Work: 10\nRest: 0\nRounds: 1\nVibrate: yes\n", ItParseCorrupt);
    T("Work: 10\nRest: 0\nRounds: 1\n", ItParseCorrupt); /* missing key */
    T("Work: 10\nWork: 11\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt); /* dup */
    T("Work: 10\nRest: 0\nRounds: 1\nVibrate: true\nExtra: 1\n", ItParseOk); /* unknown ignored */
    T("Work: 10\nRest: 0\nRounds: 1\nVibrate: true", ItParseCorrupt); /* no trailing newline */
    T("Work: 1000000000\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Work:10\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    CHECK(p("") == ItParseCorrupt);
    CHECK(p("Filetype: Other\nVersion: 2\n") == ItParseCorrupt);
    CHECK(p("Filetype: Interval Timer Settings\nVersion: 0\n") == ItParseCorrupt);
    CHECK(p("Filetype: Interval Timer Settings\nVersion: 3\ngarbage") == ItParseFuture);
    CHECK(p("Filetype: Interval Timer Settings\nVersion: 1\nWork: 5\nRest: 0\n") == ItParseMigrated);
    CHECK(p("Filetype: Interval Timer Settings\nVersion: 1\nWork: 5\n") == ItParseCorrupt);

    char big[IT_MAX_FILE + 64];
    memset(big, 'a', sizeof(big));
    ItSettings s;
    CHECK(it_settings_parse(big, sizeof(big), &s) == ItParseCorrupt);

    /* round trip */
    ItSettings in = {37, 0, 99, false}, out;
    size_t n = it_settings_format(&in, b, sizeof(b));
    CHECK(n > 0);
    CHECK(it_settings_parse(b, n, &out) == ItParseOk);
    CHECK(memcmp(&in, &out, sizeof(in)) == 0);
    CHECK(it_settings_format(&in, b, 10) == 0);
}

/* ---- in-memory filesystem with fault injection ---- */

#define NFILES 3
typedef struct {
    const char* path;
    bool present;
    char data[IT_MAX_FILE + 1];
    size_t len;
} MemFile;

typedef struct {
    MemFile f[NFILES];
    int ops; /* mutating/IO ops performed */
    int fail_from; /* ops >= fail_from fail (card removed / power lost); -1 = never */
    bool unreadable; /* all reads error */
} MemFs;

static MemFile* find(MemFs* m, const char* path) {
    for(int i = 0; i < NFILES; i++)
        if(strcmp(m->f[i].path, path) == 0) return &m->f[i];
    return NULL;
}
static bool tick(MemFs* m) {
    bool fail = m->fail_from >= 0 && m->ops >= m->fail_from;
    m->ops++;
    return !fail;
}
static ItFsStatus m_read(void* c, const char* path, char* buf, size_t cap, size_t* len) {
    MemFs* m = c;
    if(m->unreadable) return ItFsError;
    MemFile* f = find(m, path);
    if(!f->present) return ItFsNotExist;
    if(f->len > cap) return ItFsError;
    memcpy(buf, f->data, f->len);
    *len = f->len;
    return ItFsOk;
}
static bool m_write(void* c, const char* path, const char* d, size_t n) {
    MemFs* m = c;
    MemFile* f = find(m, path);
    if(!tick(m)) { /* partial write left behind */
        f->present = true;
        f->len = n / 2;
        memcpy(f->data, d, f->len);
        return false;
    }
    f->present = true;
    f->len = n;
    memcpy(f->data, d, n);
    return true;
}
static bool m_rename(void* c, const char* from, const char* to) {
    MemFs* m = c;
    if(!tick(m)) return false;
    MemFile *a = find(m, from), *b = find(m, to);
    if(!a->present || b->present) return false;
    b->present = true;
    b->len = a->len;
    memcpy(b->data, a->data, a->len);
    a->present = false;
    return true;
}
static ItFsStatus m_remove(void* c, const char* path) {
    MemFs* m = c;
    if(!tick(m)) return ItFsError;
    MemFile* f = find(m, path);
    if(!f->present) return ItFsNotExist;
    f->present = false;
    return ItFsOk;
}
static ItFsStatus m_exists(void* c, const char* path) {
    MemFs* m = c;
    if(m->unreadable) return ItFsError;
    return find(m, path)->present ? ItFsOk : ItFsNotExist;
}
static const ItFsOps ops = {m_read, m_write, m_rename, m_remove, m_exists};
static const ItPaths paths = {"cur", "tmp", "bak"};

static void mem_init(MemFs* m, const char* cur_text) {
    memset(m, 0, sizeof(*m));
    m->f[0].path = "cur";
    m->f[1].path = "tmp";
    m->f[2].path = "bak";
    m->fail_from = -1;
    if(cur_text) {
        m->f[0].present = true;
        m->f[0].len = strlen(cur_text);
        memcpy(m->f[0].data, cur_text, m->f[0].len);
    }
}

static bool same(const ItSettings* a, const ItSettings* b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void test_store(const char* dir) {
    char v1[2048];
    size_t n = read_sample(dir, "settings_v1.txt", v1, sizeof(v1) - 1);
    v1[n] = 0;
    ItSettings old_v1 = {45, 15, 8, true};
    ItSettings neu = {50, 20, 3, false};
    ItSettings got;
    MemFs m;

    /* first run */
    mem_init(&m, NULL);
    CHECK(it_store_load(&ops, &m, &paths, &got) == ItLoadNoFile);
    CHECK(it_store_save(&ops, &m, &paths, &neu));
    CHECK(it_store_load(&ops, &m, &paths, &got) == ItLoadOk && same(&got, &neu));

    /* v1 -> v2 upgrade keeps values, then save writes v2 and keeps v1 as .bak */
    mem_init(&m, v1);
    CHECK(it_store_load(&ops, &m, &paths, &got) == ItLoadMigrated && same(&got, &old_v1));
    CHECK(it_store_save(&ops, &m, &paths, &got));
    CHECK(it_store_load(&ops, &m, &paths, &got) == ItLoadOk && same(&got, &old_v1));
    CHECK(m.f[2].present);

    /* fault injection: every failure point, v1 -> new */
    for(int fail_at = 0; fail_at < 12; fail_at++) {
        mem_init(&m, v1);
        m.fail_from = fail_at;
        bool ok = it_store_save(&ops, &m, &paths, &neu);
        m.fail_from = -1;
        ItLoadResult r = it_store_load(&ops, &m, &paths, &got);
        if(ok) {
            CHECK(r == ItLoadOk && same(&got, &neu));
        } else {
            /* old OR new settings must be recoverable; never defaults/corrupt */
            CHECK(r == ItLoadMigrated || r == ItLoadOk || r == ItLoadRecoveredBackup);
            CHECK(same(&got, &old_v1) || same(&got, &neu));
        }
        CHECK(!m.f[1].present); /* stale tmp cleaned by load */
    }

    /* single failure at each step (not persistent) */
    for(int fail_at = 0; fail_at < 8; fail_at++) {
        mem_init(&m, v1);
        m.fail_from = fail_at;
        (void)it_store_save(&ops, &m, &paths, &neu);
        m.fail_from = -1;
        ItLoadResult r = it_store_load(&ops, &m, &paths, &got);
        CHECK(!it_load_blocks_save(r));
        CHECK(same(&got, &old_v1) || same(&got, &neu));
    }

    /* blocked cases never touch the existing file */
    mem_init(&m, "Filetype: Interval Timer Settings\nVersion: 7\nWork: 30\n");
    CHECK(it_store_load(&ops, &m, &paths, &got) == ItLoadFuture);
    CHECK(it_load_blocks_save(ItLoadFuture));
    mem_init(&m, "Filetype: Interval Timer Settings\nVersion: 2\nWork: 99999999999\n");
    CHECK(it_store_load(&ops, &m, &paths, &got) == ItLoadCorrupt);
    CHECK(it_load_blocks_save(ItLoadCorrupt));
    mem_init(&m, v1);
    m.unreadable = true;
    CHECK(it_store_load(&ops, &m, &paths, &got) == ItLoadUnreadable);
    CHECK(it_load_blocks_save(ItLoadUnreadable));
    CHECK(m.f[0].present); /* untouched */

    /* save with SD gone */
    mem_init(&m, v1);
    m.unreadable = true;
    CHECK(!it_store_save(&ops, &m, &paths, &neu));
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "../../samples";
    test_samples(dir);
    test_edges();
    test_store(dir);
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
