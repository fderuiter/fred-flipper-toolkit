/* Host test: gcc -Wall -Wextra -I.. test_settings.c ../interval_settings.c -o t && ./t ../../samples */
#include "interval_settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(c) \
    do { \
        checks++; \
        if(!(c)) { \
            failures++; \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); \
        } \
    } while(0)

static char sample_dir[256];

static size_t load_sample(const char* name, char* buf, size_t cap) {
    char p[400];
    snprintf(p, sizeof(p), "%s/%s", sample_dir, name);
    FILE* f = fopen(p, "rb");
    if(!f) {
        printf("cannot open %s\n", p);
        exit(2);
    }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

/* ---- in-memory FS with failure injection ---- */
#define MAXF 8
typedef struct {
    char name[IT_PATH_MAX];
    char data[512];
    size_t len;
    bool used;
} MFile;
typedef struct {
    MFile f[MAXF];
    int mutations; /* count of mutating ops */
    int fail_at; /* fail the Nth mutating op (1-based), 0 = never */
    bool read_error;
} MFs;

static MFile* find(MFs* fs, const char* p) {
    for(int i = 0; i < MAXF; i++)
        if(fs->f[i].used && !strcmp(fs->f[i].name, p)) return &fs->f[i];
    return NULL;
}
static bool inject(MFs* fs) {
    fs->mutations++;
    return fs->fail_at && fs->mutations == fs->fail_at;
}
static void put(MFs* fs, const char* p, const char* d, size_t n) {
    MFile* f = find(fs, p);
    for(int i = 0; !f && i < MAXF; i++)
        if(!fs->f[i].used) f = &fs->f[i];
    f->used = true;
    snprintf(f->name, sizeof(f->name), "%s", p);
    memcpy(f->data, d, n);
    f->len = n;
}
static ItIo m_read(void* c, const char* p, char* b, size_t cap, size_t* len) {
    MFs* fs = c;
    if(fs->read_error) return ItIoError;
    MFile* f = find(fs, p);
    if(!f) return ItIoNotFound;
    *len = f->len < cap ? f->len : cap;
    memcpy(b, f->data, *len);
    return ItIoOk;
}
static ItIo m_write(void* c, const char* p, const char* d, size_t n) {
    MFs* fs = c;
    if(inject(fs)) {
        put(fs, p, d, n / 2); /* torn write */
        return ItIoError;
    }
    put(fs, p, d, n);
    return ItIoOk;
}
static ItIo m_rename(void* c, const char* a, const char* b) {
    MFs* fs = c;
    if(inject(fs)) return ItIoError;
    MFile* f = find(fs, a);
    if(!f) return ItIoNotFound;
    if(find(fs, b)) return ItIoError; /* like FatFs: no overwrite */
    snprintf(f->name, sizeof(f->name), "%s", b);
    return ItIoOk;
}
static ItIo m_remove(void* c, const char* p) {
    MFs* fs = c;
    if(inject(fs)) return ItIoError;
    MFile* f = find(fs, p);
    if(!f) return ItIoNotFound;
    f->used = false;
    return ItIoOk;
}
static const ItStoreOps ops = {m_read, m_write, m_rename, m_remove};

#define MAIN "/data/s.txt"

static void test_parse_samples(void) {
    char b[1024];
    ItSettings s;
    size_t n = load_sample("settings_v1.txt", b, sizeof(b));
    CHECK(it_settings_parse(b, n, &s) == ItParseMigratedV1);
    CHECK(s.work_s == 45 && s.rest_s == 15); /* user values kept */
    CHECK(s.rounds == 8 && s.vibrate); /* defaults for new fields */

    n = load_sample("settings_v2.txt", b, sizeof(b));
    CHECK(it_settings_parse(b, n, &s) == ItParseOk);
    CHECK(s.work_s == 30 && s.rest_s == 10 && s.rounds == 8 && s.vibrate);

    n = load_sample("settings_v2_corrupt.txt", b, sizeof(b));
    CHECK(it_settings_parse(b, n, &s) == ItParseCorrupt);

    n = load_sample("settings_v7_future.txt", b, sizeof(b));
    CHECK(it_settings_parse(b, n, &s) == ItParseFuture);
}

static ItParseResult P(const char* t) {
    ItSettings s;
    return it_settings_parse(t, strlen(t), &s);
}

static void test_parse_edges(void) {
    const char* h = "Filetype: Interval Timer Settings\n";
    char b[300];
#define T(body, expect) \
    do { \
        snprintf(b, sizeof(b), "%s%s", h, body); \
        CHECK(P(b) == (expect)); \
    } while(0)
    T("Version: 2\nWork: 5\nRest: 0\nRounds: 1\nVibrate: false\n", ItParseOk);
    T("Version: 2\nWork: 600\nRest: 600\nRounds: 99\nVibrate: true\n", ItParseOk);
    T("Version: 2\nWork: 4\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Version: 2\nWork: 601\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Version: 2\nWork: 5\nRest: 601\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Version: 2\nWork: 5\nRest: 0\nRounds: 100\nVibrate: true\n", ItParseCorrupt);
    T("Version: 2\nWork: 5\nRest: 0\nRounds: 0\nVibrate: true\n", ItParseCorrupt);
    T("Version: 2\nWork: 5\nRest: 0\nRounds: 1\nVibrate: yes\n", ItParseCorrupt);
    T("Version: 2\nWork: 5\nRest: 0\nRounds: 1\n", ItParseCorrupt); /* missing key */
    T("Version: 2\nWork: 5\nWork: 6\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Version: 2\nWork: 5\nRest: 0\nRounds: 1\nVibrate: true", ItParseCorrupt); /* no final \n */
    T("Version: 2\nWork: 5\nRest: 0\nRounds: 1\nVibrate: true\nExtra: 1\n", ItParseOk);
    T("Version: 2\nWork: 5x\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Version: 2\nWork: 1234567890\nRest: 0\nRounds: 1\nVibrate: true\n", ItParseCorrupt);
    T("Version: 0\nWork: 5\n", ItParseCorrupt);
    T("Version: abc\n", ItParseCorrupt);
    T("Version: 3\ngarbage without colon\n", ItParseFuture); /* body not interpreted */
    T("Version: 1\nWork: 9999\nRest: 5\n", ItParseMigratedV1); /* v1 clamps */
    T("Version: 1\nWork: 45\n", ItParseCorrupt);
    T("Version: 2\r\nWork: 5\r\nRest: 0\r\nRounds: 1\r\nVibrate: true\r\n", ItParseOk); /* CRLF tolerated */
    CHECK(P("Filetype: Other\nVersion: 2\n") == ItParseCorrupt);
    CHECK(P("") == ItParseCorrupt);
    {
        char big[IT_FILE_MAX + 50];
        memset(big, 'a', sizeof(big));
        CHECK(it_settings_parse(big, sizeof(big), &(ItSettings){0}) == ItParseCorrupt);
        CHECK(it_settings_parse("a\0b\n", 4, &(ItSettings){0}) == ItParseCorrupt);
    }
    ItSettings s;
    snprintf(b, sizeof(b), "%sVersion: 1\nWork: 9999\nRest: 5\n", h);
    it_settings_parse(b, strlen(b), &s);
    CHECK(s.work_s == 600 && s.rest_s == 5);
}

static void test_roundtrip_and_index(void) {
    ItSettings s = {.work_s = 123, .rest_s = 0, .rounds = 99, .vibrate = false}, o;
    char b[IT_FILE_MAX];
    size_t n = it_settings_format(&s, b, sizeof(b));
    CHECK(n > 0 && it_settings_parse(b, n, &o) == ItParseOk);
    CHECK(o.work_s == 123 && o.rest_s == 0 && o.rounds == 99 && !o.vibrate);
    CHECK(it_settings_format(&s, b, 10) == 0);

    CHECK(it_secs_count(IT_WORK_MIN, IT_WORK_MAX) <= 255);
    CHECK(it_secs_count(IT_REST_MIN, IT_REST_MAX) <= 255);
    for(unsigned v = 5; v <= 600; v++) {
        uint8_t i = it_secs_to_index(v, 5, 600);
        CHECK(i < it_secs_count(5, 600));
        uint16_t back = it_secs_from_index(i, 5, 600);
        CHECK(v <= 60 ? back == v : (back >= v - 2 && back <= v + 2));
    }
    CHECK(it_secs_from_index(0, 5, 600) == 5);
    CHECK(it_secs_from_index(it_secs_count(5, 600) - 1, 5, 600) == 600);
    CHECK(it_secs_from_index(0, 0, 600) == 0);
    CHECK(it_secs_from_index(it_secs_count(0, 600) - 1, 0, 600) == 600);
    /* monotonic */
    for(unsigned i = 1; i < it_secs_count(0, 600); i++)
        CHECK(it_secs_from_index(i, 0, 600) > it_secs_from_index(i - 1, 0, 600));
}

static void test_store_basic(void) {
    MFs fs;
    ItStore st;
    ItSettings s;
    char b[1024];

    memset(&fs, 0, sizeof(fs));
    CHECK(it_store_init(&st, &ops, &fs, MAIN));
    CHECK(it_store_load(&st, &s) == ItLoadFresh);

    /* v1 upgrade keeps values, and save rotates v1 to .bak */
    size_t n = load_sample("settings_v1.txt", b, sizeof(b));
    put(&fs, MAIN, b, n);
    CHECK(it_store_load(&st, &s) == ItLoadMigrated && s.work_s == 45 && s.rest_s == 15);
    CHECK(it_store_save(&st, &s));
    CHECK(find(&fs, MAIN ".bak") != NULL);
    CHECK(it_store_load(&st, &s) == ItLoadOk && s.work_s == 45 && s.rest_s == 15);

    /* future: read-only, file untouched */
    memset(&fs, 0, sizeof(fs));
    n = load_sample("settings_v7_future.txt", b, sizeof(b));
    put(&fs, MAIN, b, n);
    CHECK(it_store_load(&st, &s) == ItLoadFuture);
    CHECK(!it_store_save(&st, &s));
    CHECK(find(&fs, MAIN) && find(&fs, MAIN)->len == n && !find(&fs, MAIN ".bak"));

    /* corrupt: defaults, bad kept on save, never lost */
    memset(&fs, 0, sizeof(fs));
    n = load_sample("settings_v2_corrupt.txt", b, sizeof(b));
    put(&fs, MAIN, b, n);
    CHECK(it_store_load(&st, &s) == ItLoadCorrupt && s.work_s == 30);
    CHECK(it_store_save(&st, &s));
    CHECK(find(&fs, MAIN ".bad") && find(&fs, MAIN ".bad")->len == n);
    CHECK(it_store_load(&st, &s) == ItLoadOk);

    /* read error: blocked */
    fs.read_error = true;
    CHECK(it_store_load(&st, &s) == ItLoadIoError);
    CHECK(!it_store_save(&st, &s));
}

/* Fail every mutating step of a save; the next load must yield old or new. */
static void test_store_fault_injection(void) {
    ItSettings oldv = {.work_s = 45, .rest_s = 15, .rounds = 3, .vibrate = false};
    ItSettings newv = {.work_s = 20, .rest_s = 5, .rounds = 9, .vibrate = true};
    char b[IT_FILE_MAX];
    size_t n = it_settings_format(&oldv, b, sizeof(b));

    for(int second_save = 0; second_save < 2; second_save++) {
        for(int k = 1; k <= 6; k++) {
            MFs fs;
            ItStore st;
            ItSettings s;
            memset(&fs, 0, sizeof(fs));
            put(&fs, MAIN, b, n);
            if(second_save) { /* also have a .bak present */
                put(&fs, MAIN ".bak", b, n);
            }
            it_store_init(&st, &ops, &fs, MAIN);
            CHECK(it_store_load(&st, &s) == ItLoadOk);
            fs.mutations = 0;
            fs.fail_at = k;
            bool ok = it_store_save(&st, &newv);

            ItStore st2;
            it_store_init(&st2, &ops, &fs, MAIN);
            fs.fail_at = 0;
            ItLoadStatus status = it_store_load(&st2, &s);
            bool is_old = s.work_s == 45 && s.rest_s == 15 && s.rounds == 3 && !s.vibrate;
            bool is_new = s.work_s == 20 && s.rest_s == 5 && s.rounds == 9 && s.vibrate;
            if(!(is_old || is_new)) printf("  k=%d ok=%d status=%d\n", k, ok, status);
            CHECK(is_old || is_new);
            if(ok) CHECK(is_new);
            CHECK(status == ItLoadOk || status == ItLoadRecovered);
        }
    }
}

static void test_run(void) {
    ItSettings s = {.work_s = 5, .rest_s = 3, .rounds = 2, .vibrate = true};
    ItRun r;
    it_run_start(&r, &s);
    CHECK(r.phase == ItPhaseWork && r.round == 1 && r.remaining_ms == 5000);
    CHECK(it_run_advance(&r, &s, 1000) == ItEvtNone && r.remaining_ms == 4000);
    CHECK(it_run_advance(&r, &s, 4000) == ItEvtPhaseChanged && r.phase == ItPhaseRest);
    CHECK(it_run_advance(&r, &s, 3500) == ItEvtPhaseChanged && r.phase == ItPhaseWork);
    CHECK(r.round == 2 && r.remaining_ms == 4500);
    CHECK(it_run_advance(&r, &s, 4500) == ItEvtFinished && r.phase == ItPhaseDone);
    CHECK(it_run_advance(&r, &s, 100) == ItEvtNone && r.phase == ItPhaseDone);

    /* no rest, huge stall: finishes, no overflow/hang */
    s.rest_s = 0;
    s.rounds = 99;
    it_run_start(&r, &s);
    CHECK(it_run_advance(&r, &s, 0xFFFFFFFFu) == ItEvtFinished);
    s.rounds = 1;
    s.rest_s = 600;
    it_run_start(&r, &s);
    CHECK(it_run_advance(&r, &s, 5000) == ItEvtFinished); /* no rest after last round */
}

int main(int argc, char** argv) {
    snprintf(sample_dir, sizeof(sample_dir), "%s", argc > 1 ? argv[1] : "../../samples");
    test_parse_samples();
    test_parse_edges();
    test_roundtrip_and_index();
    test_store_basic();
    test_store_fault_injection();
    test_run();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
