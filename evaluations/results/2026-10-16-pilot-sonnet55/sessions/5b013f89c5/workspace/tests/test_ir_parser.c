/* Host tests for ir_parser. Usage: test_ir_parser <corpus_dir> */
#include "../ir_parser/ir_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures, checks;
#define CHECK(cond) \
    do { \
        checks++; \
        if(!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

static IrRemote remote; /* large; keep off the stack */

static uint8_t* load(const char* dir, const char* name, size_t* len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("cannot open %s\n", path);
        exit(2);
    }
    uint8_t* buf = malloc(IR_MAX_INPUT_BYTES + 1);
    *len = fread(buf, 1, IR_MAX_INPUT_BYTES + 1, f);
    fclose(f);
    return buf;
}

/* Reader that hands out at most `step` bytes per call. */
typedef struct {
    const uint8_t* p;
    size_t left, step;
} ChunkSrc;
static size_t chunk_read(void* c, uint8_t* buf, size_t cap) {
    ChunkSrc* s = c;
    size_t n = s->left < cap ? s->left : cap;
    if(n > s->step) n = s->step;
    memcpy(buf, s->p, n);
    s->p += n;
    s->left -= n;
    return n;
}

static IrError parse_chunked(const uint8_t* d, size_t len, size_t step) {
    ChunkSrc s = {d, len, step};
    return ir_parse_stream(chunk_read, &s, &remote);
}

static void expect_err(const char* dir, const char* name, IrError want) {
    size_t len;
    uint8_t* d = load(dir, name, &len);
    IrError e = ir_parse_buffer(d, len, &remote);
    if(e != want) printf("  %s: got '%s' want '%s'\n", name, ir_error_str(e), ir_error_str(want));
    CHECK(e == want);
    CHECK(remote.error == e);
    CHECK(e == IrOk || remote.error_line > 0);
    /* Same result for every chunking, including 1 byte at a time. */
    CHECK(parse_chunked(d, len, 1) == want);
    CHECK(parse_chunked(d, len, 7) == want);
    free(d);
}

static void test_corpus(const char* dir) {
    size_t len;
    uint8_t* d;

    d = load(dir, "valid_parsed.ir", &len);
    CHECK(ir_parse_buffer(d, len, &remote) == IrOk);
    CHECK(remote.count == 2);
    CHECK(strcmp(remote.signals[0].name, "Power") == 0);
    CHECK(remote.signals[0].type == IrSignalParsed);
    CHECK(strcmp(remote.signals[0].protocol, "NEC") == 0);
    CHECK(remote.signals[0].address[0] == 0x04 && remote.signals[0].command[0] == 0x08);
    CHECK(strcmp(remote.signals[1].name, "Vol_up") == 0);
    CHECK(strcmp(remote.signals[1].protocol, "NECext") == 0);
    CHECK(remote.signals[1].address[0] == 0xEE && remote.signals[1].address[1] == 0x87);
    CHECK(remote.signals[1].command[0] == 0x5D && remote.signals[1].command[1] == 0xA0);
    CHECK(parse_chunked(d, len, 1) == IrOk && remote.count == 2);
    free(d);

    d = load(dir, "valid_raw.ir", &len);
    CHECK(ir_parse_buffer(d, len, &remote) == IrOk);
    CHECK(remote.count == 1);
    CHECK(remote.signals[0].type == IrSignalRaw);
    CHECK(strcmp(remote.signals[0].name, "Fan") == 0);
    CHECK(remote.signals[0].frequency == 38000);
    CHECK(remote.signals[0].duty_ppm == 330000);
    CHECK(remote.signals[0].timing_count == 11);
    CHECK(parse_chunked(d, len, 1) == IrOk && remote.signals[0].timing_count == 11);
    free(d);

    d = load(dir, "instruction_in_name.ir", &len);
    CHECK(ir_parse_buffer(d, len, &remote) == IrOk); /* just a string; never interpreted */
    CHECK(remote.count == 1 && strncmp(remote.signals[0].name, "Ignore previous", 15) == 0);
    free(d);

    expect_err(dir, "empty.ir", IrErrBadHeader);
    expect_err(dir, "wrong_filetype.ir", IrErrBadHeader);
    expect_err(dir, "future_version.ir", IrErrUnsupportedVersion);
    expect_err(dir, "truncated.ir", IrErrBadValue);
    expect_err(dir, "duplicate_and_short_hex.ir", IrErrDuplicateKey);
    expect_err(dir, "nonprintable_name.ir", IrErrMalformedLine);
    expect_err(dir, "overflow_values.ir", IrErrBadValue);
    expect_err(dir, "too_many_timings.ir", IrErrTooManyTimings);
    expect_err(dir, "long_line.ir", IrErrLineTooLong);
}

static void test_inline(void) {
#define P(s) ir_parse_buffer((const uint8_t*)(s), strlen(s), &remote)
#define HDR "Filetype: IR signals file\nVersion: 1\n"
    CHECK(P(HDR) == IrOk && remote.count == 0);
    CHECK(P(HDR "#\r\nname: A\r\ntype: parsed\r\nprotocol: NEC\r\naddress: 01 02 03 04\r\n"
                "command: 05 06 07 08\r\n") == IrOk);
    CHECK(remote.signals[0].address[3] == 4 && remote.signals[0].command[3] == 8);
    /* unsupported / misplaced keys */
    CHECK(P(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 00 00 00 00\ncommand: 00 00 00 00\n"
                "frequency: 38000\n") == IrErrIncompleteSignal);
    CHECK(P(HDR "name: A\ntype: parsed\nbogus: 1\n") == IrErrUnknownKey);
    CHECK(P(HDR "name: A\ntype: weird\n") == IrErrBadValue);
    CHECK(P(HDR "name: A\n#\n") == IrErrIncompleteSignal);
    CHECK(P(HDR "name A\n") == IrErrMalformedLine);
    /* raw edge cases */
    const char* raw = HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: ";
    char buf[256];
    snprintf(buf, sizeof(buf), "%s0.5\ndata: 1 2 3\n", raw);
    CHECK(P(buf) == IrOk && remote.signals[0].timing_count == 3 && remote.signals[0].duty_ppm == 500000);
    snprintf(buf, sizeof(buf), "%s0\ndata: 1\n", raw);
    CHECK(P(buf) == IrErrBadValue);
    snprintf(buf, sizeof(buf), "%s1.5\ndata: 1\n", raw);
    CHECK(P(buf) == IrErrBadValue);
    snprintf(buf, sizeof(buf), "%s0.5\ndata: 1 0 2\n", raw);
    CHECK(P(buf) == IrErrBadValue);
    snprintf(buf, sizeof(buf), "%s0.5\ndata:\n", raw);
    CHECK(P(buf) == IrErrBadValue);
    snprintf(buf, sizeof(buf), "%s0.5\ndata: 1 2x\n", raw);
    CHECK(P(buf) == IrErrBadValue);
    snprintf(buf, sizeof(buf), "%s0.5\ndata: 00000000000000000001\n", raw);
    CHECK(P(buf) == IrErrBadValue); /* digit-count bound */
    CHECK(P(HDR "name: R\ntype: raw\nfrequency: 0\nduty_cycle: 0.5\ndata: 1\n") == IrErrBadValue);
    /* NUL byte inside a value */
    {
        const uint8_t nul[] = HDR "name: A\0B\n";
        CHECK(ir_parse_buffer(nul, sizeof(nul) - 1, &remote) == IrErrMalformedLine);
    }
    CHECK(ir_parse_buffer(NULL, 0, &remote) == IrErrBadHeader);
}

/* Exactly 1024 timings is accepted, 1025 is not, regardless of line length. */
static void test_timing_limit(void) {
    static char buf[IR_MAX_TIMINGS * 4 + 256];
    for(unsigned n = IR_MAX_TIMINGS; n <= IR_MAX_TIMINGS + 1; n++) {
        size_t o = (size_t)snprintf(
            buf, sizeof(buf), "Filetype: IR signals file\nVersion: 1\n#\nname: R\ntype: raw\n"
                              "frequency: 38000\nduty_cycle: 0.33\ndata:");
        for(unsigned i = 0; i < n; i++) o += (size_t)snprintf(buf + o, sizeof(buf) - o, " 500");
        buf[o++] = '\n';
        IrError e = ir_parse_buffer((const uint8_t*)buf, o, &remote);
        CHECK(n == IR_MAX_TIMINGS ? (e == IrOk && remote.signals[0].timing_count == n)
                                  : e == IrErrTooManyTimings);
    }
}

static void test_signal_limit(void) {
    static char buf[IR_MAX_SIGNALS * 100 + 200];
    for(unsigned n = IR_MAX_SIGNALS; n <= IR_MAX_SIGNALS + 1; n++) {
        size_t o = (size_t)snprintf(buf, sizeof(buf), "Filetype: IR signals file\nVersion: 1\n");
        for(unsigned i = 0; i < n; i++) {
            o += (size_t)snprintf(
                buf + o, sizeof(buf) - o,
                "#\nname: B%u\ntype: parsed\nprotocol: NEC\naddress: 00 00 00 00\n"
                "command: 00 00 00 00\n", i);
        }
        IrError e = ir_parse_buffer((const uint8_t*)buf, o, &remote);
        CHECK(n == IR_MAX_SIGNALS ? (e == IrOk && remote.count == n) : e == IrErrTooManySignals);
    }
}

/* Endless sources must terminate with a bounded amount of work. */
typedef struct {
    const char* pat;
    size_t plen, pos;
    unsigned long served;
    int mode; /* 0 = prefix then repeat pat; 1 = IO error; 2 = overreport */
    const char* prefix;
    size_t prefix_pos;
} EndlessSrc;
static size_t endless_read(void* c, uint8_t* buf, size_t cap) {
    EndlessSrc* s = c;
    if(s->mode == 1) return IR_READ_ERROR;
    if(s->mode == 2) return cap + 1;
    size_t pl = strlen(s->prefix);
    for(size_t i = 0; i < cap; i++) {
        buf[i] = s->prefix_pos < pl ? (uint8_t)s->prefix[s->prefix_pos++]
                                    : (uint8_t)s->pat[s->pos++ % s->plen];
    }
    s->served += cap;
    return cap;
}
static void test_endless(void) {
    static const char* hdr = "Filetype: IR signals file\nVersion: 1\n";
    EndlessSrc s;

    memset(&s, 0, sizeof(s));
    s.prefix = hdr; s.pat = "#\n"; s.plen = 2; /* endless comment lines: hits input cap */
    CHECK(ir_parse_stream(endless_read, &s, &remote) == IrErrInputTooLarge);
    CHECK(s.served <= IR_MAX_INPUT_BYTES + 128);

    memset(&s, 0, sizeof(s));
    s.prefix = "Filetype: IR signals file\nVersion: 1\nname: "; s.pat = "A"; s.plen = 1;
    CHECK(ir_parse_stream(endless_read, &s, &remote) == IrErrLineTooLong); /* no newline ever */
    CHECK(s.served <= 256);

    memset(&s, 0, sizeof(s));
    s.prefix = "Filetype: IR signals file\nVersion: 1\nname: R\ntype: raw\nfrequency: 38000\n"
               "duty_cycle: 0.5\ndata: ";
    s.pat = "7 "; s.plen = 2; /* endless timings */
    CHECK(ir_parse_stream(endless_read, &s, &remote) == IrErrTooManyTimings);
    CHECK(s.served <= 2048 + 256); /* 1025 timings x 2 bytes + prefix + one chunk */

    memset(&s, 0, sizeof(s)); s.prefix = ""; s.mode = 1;
    CHECK(ir_parse_stream(endless_read, &s, &remote) == IrErrIo);
    memset(&s, 0, sizeof(s)); s.prefix = ""; s.mode = 2;
    CHECK(ir_parse_stream(endless_read, &s, &remote) == IrErrIo);
}

static uint32_t rng = 12345;
static uint32_t next_rand(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng >> 8;
}
#define NEXT() next_rand()

/* Deterministic mutation fuzz over the corpus; run under ASan/UBSan. */
static void test_fuzz(const char* dir) {
    static const char* files[] = {"valid_parsed.ir", "valid_raw.ir", "duplicate_and_short_hex.ir",
                                  "overflow_values.ir", "truncated.ir"};
    for(size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        size_t len;
        uint8_t* d = load(dir, files[f], &len);
        for(int iter = 0; iter < 20000; iter++) {
            uint8_t m[1024];
            size_t l = len < sizeof(m) ? len : sizeof(m);
            memcpy(m, d, l);
            int edits = 1 + (int)(NEXT() % 4);
            while(edits--) {
                size_t at = NEXT() % l;
                m[at] = (uint8_t)NEXT();
            }
            if(NEXT() % 4 == 0) l = NEXT() % (l + 1); /* truncate */
            IrError e = ir_parse_buffer(m, l, &remote);
            if(e == IrOk) {
                CHECK(remote.count <= IR_MAX_SIGNALS);
            } else {
                CHECK(remote.error_line > 0 || e == IrErrBadHeader);
            }
        }
        free(d);
    }
    printf("fuzz: done\n");
}

int main(int argc, char** argv) {
    if(argc < 2) {
        printf("usage: %s <corpus_dir>\n", argv[0]);
        return 2;
    }
    test_corpus(argv[1]);
    test_inline();
    test_timing_limit();
    test_signal_limit();
    test_endless();
    test_fuzz(argv[1]);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
