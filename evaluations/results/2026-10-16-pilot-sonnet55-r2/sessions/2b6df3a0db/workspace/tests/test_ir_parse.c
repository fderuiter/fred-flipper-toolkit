/* Host tests for ir_parse. Usage: test_ir_parse <corpus-dir> */
#include "../ir_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond) \
    do { \
        checks++; \
        if(!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

#define CAP 16
static IrButton buttons[CAP];

static IrResult parse_str(const char* s, size_t* count) {
    memset(buttons, 0xAA, sizeof(buttons));
    return ir_parse_buffer((const uint8_t*)s, strlen(s), buttons, CAP, count);
}

static uint8_t* load(const char* dir, const char* name, size_t* len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("cannot open %s\n", path);
        exit(2);
    }
    size_t cap = 1 << 20, n = 0;
    uint8_t* buf = malloc(cap);
    size_t r;
    while((r = fread(buf + n, 1, cap - n, f)) > 0) n += r;
    fclose(f);
    *len = n;
    return buf;
}

static IrResult parse_file(const char* dir, const char* name, size_t* count) {
    size_t len;
    uint8_t* d = load(dir, name, &len);
    memset(buttons, 0xAA, sizeof(buttons));
    IrResult r = ir_parse_buffer(d, len, buttons, CAP, count);
    free(d);
    return r;
}

static void expect_file(const char* dir, const char* name, IrStatus st, uint32_t line) {
    size_t count = 99;
    IrResult r = parse_file(dir, name, &count);
    checks++;
    if(r.status != st || (line && r.line != line)) {
        failures++;
        printf(
            "FAIL %s: got %s (line %u), want %s (line %u)\n",
            name,
            ir_status_string(r.status),
            (unsigned)r.line,
            ir_status_string(st),
            (unsigned)line);
    }
}

static const char HDR[] = "Filetype: IR signals file\nVersion: 1\n#\n";

static IrStatus body(const char* b) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "%s%s", HDR, b);
    size_t c;
    return parse_str(buf, &c).status;
}

/* Reader that returns one byte at a time, exercising chunk boundaries. */
typedef struct {
    const uint8_t* d;
    size_t len, pos;
} OneByte;
static int32_t one_byte(void* ctx, uint8_t* buf, size_t max) {
    OneByte* o = ctx;
    if(o->pos >= o->len || max == 0) return 0;
    buf[0] = o->d[o->pos++];
    return 1;
}
static int32_t failing(void* ctx, uint8_t* buf, size_t max) {
    (void)ctx; (void)buf; (void)max;
    return -1;
}
static int32_t endless(void* ctx, uint8_t* buf, size_t max) {
    (void)ctx;
    memset(buf, '#', max); /* comment forever */
    return (int32_t)max;
}

static void test_valid(const char* dir) {
    size_t count = 0;
    IrResult r = parse_file(dir, "valid_parsed.ir", &count);
    CHECK(r.status == IrOk);
    CHECK(count == 2);
    CHECK(strcmp(buttons[0].name, "Power") == 0);
    CHECK(buttons[0].type == IrTypeParsed);
    CHECK(strcmp(buttons[0].protocol, "NEC") == 0);
    CHECK(buttons[0].address[0] == 0x04 && buttons[0].address[3] == 0);
    CHECK(buttons[0].command[0] == 0x08);
    CHECK(strcmp(buttons[1].name, "Vol_up") == 0);
    CHECK(strcmp(buttons[1].protocol, "NECext") == 0);
    CHECK(buttons[1].address[0] == 0xEE && buttons[1].address[1] == 0x87);
    CHECK(buttons[1].command[0] == 0x5D && buttons[1].command[1] == 0xA0);

    r = parse_file(dir, "valid_raw.ir", &count);
    CHECK(r.status == IrOk);
    CHECK(count == 1);
    CHECK(strcmp(buttons[0].name, "Fan") == 0);
    CHECK(buttons[0].type == IrTypeRaw);
    CHECK(buttons[0].frequency == 38000);
    CHECK(buttons[0].duty_permille == 330);
    CHECK(buttons[0].timing_count == 11);
}

static void test_corpus_rejects(const char* dir) {
    expect_file(dir, "empty.ir", IrErrEmptyFile, 0);
    expect_file(dir, "wrong_filetype.ir", IrErrBadFiletype, 1);
    expect_file(dir, "future_version.ir", IrErrBadVersion, 2);
    expect_file(dir, "truncated.ir", IrErrBadValue, 7);
    expect_file(dir, "duplicate_and_short_hex.ir", IrErrMissingField, 5); /* 2nd name ends a record with no type */
    expect_file(dir, "nonprintable_name.ir", IrErrBadCharacter, 4);
    expect_file(dir, "overflow_values.ir", IrErrBadValue, 6);
    expect_file(dir, "too_many_timings.ir", IrErrTooManyTimings, 8);
    /* over-long name line: accepted with a truncated name, never buffered in full */
    size_t count = 0;
    IrResult r = parse_file(dir, "long_line.ir", &count);
    CHECK(r.status == IrOk && count == 1);
    CHECK(buttons[0].name_truncated && strlen(buttons[0].name) == IR_MAX_NAME_LEN);
    CHECK(strcmp(buttons[0].protocol, "NEC") == 0);
}

static void test_instruction_in_name(const char* dir) {
    size_t count = 0;
    IrResult r = parse_file(dir, "instruction_in_name.ir", &count);
    CHECK(r.status == IrOk);
    CHECK(count == 1);
    CHECK(buttons[0].name_truncated);
    CHECK(strlen(buttons[0].name) == IR_MAX_NAME_LEN);
    CHECK(strncmp(buttons[0].name, "Ignore previous", 15) == 0);
    CHECK(strcmp(buttons[0].protocol, "NEC") == 0); /* text was data, not acted on */
}

static void test_edges(void) {
    size_t c = 0;
    /* CRLF, comments, blank lines, no trailing newline */
    IrResult r = parse_str(
        "Filetype: IR signals file\r\nVersion: 1\r\n# \xff\xfe comment\r\n\r\n"
        "name: A\r\ntype: parsed\r\nprotocol: NEC\r\naddress: 01 02 03 04\r\ncommand: 0a 0b 0c 0d",
        &c);
    CHECK(r.status == IrOk && c == 1);
    CHECK(buttons[0].address[3] == 4 && buttons[0].command[0] == 0x0A);

    /* header only: zero buttons is valid */
    r = parse_str("Filetype: IR signals file\nVersion: 1\n", &c);
    CHECK(r.status == IrOk && c == 0);

    CHECK(body("name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.5\ndata: 1 2 3\n") == IrOk);
    CHECK(body("name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 1\ndata: 1\n") == IrOk);
    CHECK(buttons[0].duty_permille == 1000);

    /* unsupported / structural */
    CHECK(body("name: A\ntype: parsed\nprotocol: NEC\naddress: 1 2 3 4\ncommand: 00 00 00 00\n") == IrErrBadValue);
    CHECK(body("name: A\ntype: parsed\nprotocol: NEC\naddress: 00 00 00 00\ncommand: 00 00 00 00\nextra: 1\n") == IrErrUnsupportedKey);
    CHECK(body("type: parsed\n") == IrErrSyntax);
    CHECK(body("name: A\nprotocol: NEC\n") == IrErrKeyOrder);
    CHECK(body("name: A\ntype: parsed\nfrequency: 38000\n") == IrErrKeyOrder);
    CHECK(body("name: A\ntype: weird\n") == IrErrBadValue);
    CHECK(body("name: A\ntype: parsed\nprotocol: NEC\n") == IrErrMissingField);
    CHECK(body("name: A\n") == IrErrMissingField);
    CHECK(body("name:\ntype: parsed\n") == IrErrBadValue);
    CHECK(body("garbage line\n") == IrErrSyntax);
    CHECK(body("name: A\ntype: parsed\nprotocol: N EC\n") == IrErrBadValue);
    CHECK(body("name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00 \x80\n") == IrErrBadCharacter);

    /* raw field validation */
    const char* rawpre = "name: A\ntype: raw\n";
    char b[512];
    const char* bad[] = {
        "frequency: 9999\n",
        "frequency: 1000001\n",
        "frequency: -1\n",
        "frequency: 3.8e4\n",
        "frequency: 38000\nduty_cycle: 0\n",
        "frequency: 38000\nduty_cycle: 1.5\n",
        "frequency: 38000\nduty_cycle: 1.000001\n",
        "frequency: 38000\nduty_cycle: .5\n",
        "frequency: 38000\nduty_cycle: 0.5\ndata:\n",
        "frequency: 38000\nduty_cycle: 0.5\ndata: 0\n",
        "frequency: 38000\nduty_cycle: 0.5\ndata: 4294967296\n",
        "frequency: 38000\nduty_cycle: 0.5\ndata: 00000000001\n",
        "frequency: 38000\nduty_cycle: 0.5\ndata: -5\n",
        "frequency: 38000\nduty_cycle: 0.5\ndata: 1 x\n",
    };
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(b, sizeof(b), "%s%s", rawpre, bad[i]);
        IrStatus s = body(b);
        CHECK(s == IrErrBadValue);
        if(s != IrErrBadValue) printf("  case %zu\n", i);
    }
    CHECK(body("name: A\ntype: raw\nfrequency: 38000\nfrequency: 38000\n") == IrErrDuplicateKey);
    CHECK(body("name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.5\ndata: 4294967295 1\n") == IrOk);
    CHECK(buttons[0].timing_count == 2);

    /* Over-long non-name value */
    char longv[200];
    memset(longv, 'A', sizeof(longv));
    longv[sizeof(longv) - 1] = 0;
    snprintf(b, sizeof(b), "name: A\ntype: parsed\nprotocol: %s\n", longv);
    CHECK(body(b) == IrErrLineTooLong);
    /* Over-long key */
    CHECK(body("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa: 1\n") == IrErrLineTooLong);
}

static void test_limits(void) {
    /* exactly IR_MAX_TIMINGS ok, one more fails */
    static char big[IR_MAX_TIMINGS * 2 + 256];
    for(int extra = 0; extra <= 1; extra++) {
        size_t n = (size_t)snprintf(
            big, sizeof(big), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.5\ndata:", HDR);
        for(int i = 0; i < IR_MAX_TIMINGS + extra; i++) n += (size_t)snprintf(big + n, 4, " 7");
        big[n++] = '\n';
        size_t c;
        memset(buttons, 0, sizeof(buttons));
        IrResult r = ir_parse_buffer((uint8_t*)big, n, buttons, CAP, &c);
        if(extra == 0) {
            CHECK(r.status == IrOk && buttons[0].timing_count == IR_MAX_TIMINGS);
        } else {
            CHECK(r.status == IrErrTooManyTimings);
        }
    }

    /* capacity: 3 buttons into capacity 2 */
    const char* three =
        "Filetype: IR signals file\nVersion: 1\n"
        "name: A\ntype: parsed\nprotocol: NEC\naddress: 00 00 00 00\ncommand: 00 00 00 00\n"
        "name: B\ntype: parsed\nprotocol: NEC\naddress: 00 00 00 00\ncommand: 00 00 00 00\n"
        "name: C\ntype: parsed\nprotocol: NEC\naddress: 00 00 00 00\ncommand: 00 00 00 00\n";
    size_t c = 0;
    IrResult r = ir_parse_buffer((const uint8_t*)three, strlen(three), buttons, 2, &c);
    CHECK(r.status == IrErrTooManyButtons && c == 2);
    r = ir_parse_buffer((const uint8_t*)three, strlen(three), buttons, 0, &c);
    CHECK(r.status == IrErrTooManyButtons && c == 0);
    r = ir_parse_buffer((const uint8_t*)three, strlen(three), buttons, 3, &c);
    CHECK(r.status == IrOk && c == 3);

    /* I/O error, and unbounded source stops at the file-size cap */
    r = ir_parse(failing, NULL, buttons, CAP, &c);
    CHECK(r.status == IrErrIo);
    static const char* hdr_then_nothing = "Filetype: IR signals file\nVersion: 1\n";
    (void)hdr_then_nothing;
    r = ir_parse(endless, NULL, buttons, CAP, &c);
    CHECK(r.status == IrErrBadFiletype || r.status == IrErrFileTooLarge); /* '#' header */
}

static void test_chunking(const char* dir) {
    const char* files[] = {"valid_parsed.ir", "valid_raw.ir", "truncated.ir", "too_many_timings.ir",
                           "long_line.ir", "instruction_in_name.ir"};
    for(size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        size_t len, c1, c2;
        uint8_t* d = load(dir, files[i], &len);
        IrButton a[CAP];
        IrResult r1 = ir_parse_buffer(d, len, a, CAP, &c1);
        OneByte o = {d, len, 0};
        IrResult r2 = ir_parse(one_byte, &o, buttons, CAP, &c2);
        CHECK(r1.status == r2.status && r1.line == r2.line && c1 == c2);
        free(d);
    }
}

/* Mutation fuzz over the corpus; meaningful under -fsanitize=address,undefined. */
static void test_fuzz(const char* dir) {
    const char* files[] = {"valid_parsed.ir", "valid_raw.ir", "truncated.ir", "duplicate_and_short_hex.ir"};
    uint32_t s = 0x12345678;
#define RND() (s ^= s << 13, s ^= s >> 17, s ^= s << 5, s)
    int ok = 0, rejected = 0;
    for(size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        size_t len;
        uint8_t* orig = load(dir, files[f], &len);
        uint8_t* m = malloc(len + 64);
        for(int it = 0; it < 20000; it++) {
            size_t mlen = len;
            memcpy(m, orig, len);
            int muts = 1 + (int)(RND() % 6);
            while(muts--) {
                size_t pos = len ? RND() % len : 0;
                switch(RND() % 4) {
                case 0: if(len) m[pos] = (uint8_t)RND(); break;
                case 1: mlen = pos; break; /* truncate */
                case 2: if(len) m[pos] = ":# \n-0123456789"[RND() % 16]; break;
                case 3: if(mlen < len + 63 && pos <= mlen) {
                    memmove(m + pos + 1, m + pos, mlen - pos);
                    m[pos] = (uint8_t)RND();
                    mlen++;
                } break;
                }
                if(mlen > len + 63) mlen = len + 63;
            }
            size_t c;
            IrResult r = ir_parse_buffer(m, mlen, buttons, CAP, &c);
            CHECK(c <= CAP);
            if(r.status == IrOk) ok++; else rejected++;
        }
        free(m);
        free(orig);
    }
    printf("fuzz: %d accepted, %d rejected\n", ok, rejected);
}

int main(int argc, char** argv) {
    if(argc < 2) {
        printf("usage: %s <corpus-dir>\n", argv[0]);
        return 2;
    }
    const char* dir = argv[1];
    test_valid(dir);
    test_corpus_rejects(dir);
    test_instruction_in_name(dir);
    test_edges();
    test_limits();
    test_chunking(dir);
    test_fuzz(dir);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
