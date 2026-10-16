/* Host tests: cc -std=c99 -Wall -Wextra -fsanitize=address,undefined \
 *   ir_parse.c test_ir_parse.c -o test_ir_parse && ./test_ir_parse ../corpus */
#include "ir_parse.h"

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

static IrParseResult* res;

static uint8_t* slurp(const char* dir, const char* name, size_t* len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("FAIL cannot open %s\n", path);
        failures++;
        *len = 0;
        return NULL;
    }
    uint8_t* b = malloc(1 << 20);
    *len = fread(b, 1, 1 << 20, f);
    fclose(f);
    return b;
}

static IrParseError parse_file(const char* dir, const char* name) {
    size_t len;
    uint8_t* b = slurp(dir, name, &len);
    IrParseError e = ir_parse_buffer(b, len, res);
    printf("%-30s -> %s (line %zu, %zu signals)\n", name, ir_parse_error_str(e), res->error_line, res->count);
    free(b);
    return e;
}

/* Reader that returns one byte per call, exercising short reads. */
typedef struct {
    const uint8_t* d;
    size_t len, pos;
} Slow;
static int32_t slow_read(void* c, uint8_t* buf, size_t max) {
    Slow* s = c;
    if(s->pos >= s->len || max == 0) return 0;
    buf[0] = s->d[s->pos++];
    return 1;
}
static int32_t failing_read(void* c, uint8_t* buf, size_t max) {
    (void)c; (void)buf; (void)max;
    return -1;
}
static int32_t endless_read(void* c, uint8_t* buf, size_t max) {
    (void)c;
    memset(buf, 'a', max);
    return (int32_t)max;
}
static int32_t lying_read(void* c, uint8_t* buf, size_t max) {
    (void)c; (void)buf;
    return (int32_t)max + 1;
}

static void test_corpus(const char* dir) {
    CHECK(parse_file(dir, "valid_parsed.ir") == IrParseOk);
    CHECK(res->count == 2);
    CHECK(strcmp(res->signals[0].name, "Power") == 0);
    CHECK(res->signals[0].type == IrSignalParsed);
    CHECK(strcmp(res->signals[0].protocol, "NEC") == 0);
    CHECK(res->signals[0].address == 4 && res->signals[0].command == 8);
    CHECK(strcmp(res->signals[1].protocol, "NECext") == 0);
    CHECK(res->signals[1].address == 0x87EEu && res->signals[1].command == 0xA05Du);

    CHECK(parse_file(dir, "valid_raw.ir") == IrParseOk);
    CHECK(res->count == 1);
    CHECK(res->signals[0].type == IrSignalRaw);
    CHECK(res->signals[0].frequency == 38000);
    CHECK(res->signals[0].duty_cycle > 0.32f && res->signals[0].duty_cycle < 0.34f);
    CHECK(res->signals[0].timings_count == 11);

    CHECK(parse_file(dir, "truncated.ir") == IrParseErrBadHex);
    CHECK(res->count == 0);
    CHECK(parse_file(dir, "too_many_timings.ir") == IrParseErrTooManyTimings);
    CHECK(parse_file(dir, "duplicate_and_short_hex.ir") == IrParseErrDuplicateKey);
    CHECK(parse_file(dir, "future_version.ir") == IrParseErrBadVersion);
    CHECK(parse_file(dir, "nonprintable_name.ir") == IrParseErrBadName);
    CHECK(parse_file(dir, "overflow_values.ir") == IrParseErrBadNumber);
    CHECK(parse_file(dir, "wrong_filetype.ir") == IrParseErrBadHeader);
    CHECK(parse_file(dir, "empty.ir") == IrParseErrTruncated);
    /* long_line.ir: must be rejected, by line or timing bound */
    IrParseError e = parse_file(dir, "long_line.ir");
    CHECK(e == IrParseErrLineTooLong || e == IrParseErrTooManyTimings);
    /* hostile text in a name is just data: accepted as a (printable) name,
     * but too long for the name bound, so rejected - never interpreted. */
    e = parse_file(dir, "instruction_in_name.ir");
    CHECK(e == IrParseErrBadName || e == IrParseOk);
    if(e == IrParseOk) CHECK(strlen(res->signals[0].name) < IR_MAX_NAME);
}

static void test_inline(void) {
    const char* hdr = "Filetype: IR signals file\nVersion: 1\n";
    char buf[4096];
    IrParseError e;

#define RUN(body) \
    snprintf(buf, sizeof(buf), "%s%s", hdr, body); \
    e = ir_parse_buffer((const uint8_t*)buf, strlen(buf), res)

    RUN("");
    CHECK(e == IrParseOk && res->count == 0); /* header only: empty remote */
    RUN("#\nname: A\ntype: parsed\nprotocol: NEC\naddress: 01 02 03 04\ncommand: 05 06 07 08\n");
    CHECK(e == IrParseOk && res->signals[0].address == 0x04030201u);
    /* CRLF, no trailing newline, no '#' separators */
    RUN("name: A\r\ntype: raw\r\nfrequency: 38000\r\nduty_cycle: 0.5\r\ndata: 1 2\r\n"
        "name: B\r\ntype: raw\r\nfrequency: 56000\r\nduty_cycle: 1\r\ndata: 7");
    CHECK(e == IrParseOk && res->count == 2 && res->signals[1].timings_count == 1);
    RUN("name: A\ntype: parsed\nprotocol: NEC\naddress: 1 2 3 4\ncommand: 05 06 07 08\n");
    CHECK(e == IrParseErrBadHex);
    RUN("name: A\ntype: parsed\nprotocol: NEC\naddress: 01020304\ncommand: 05 06 07 08\n");
    CHECK(e == IrParseErrBadHex);
    RUN("name: A\ntype: parsed\nprotocol: NEC\naddress: 01 02 03 04 05\ncommand: 05 06 07 08\n");
    CHECK(e == IrParseErrBadHex);
    RUN("name: A\ntype: parsed\nprotocol: NEC\naddress: 01 02 03 04\n");
    CHECK(e == IrParseErrMissingField);
    RUN("name: A\ntype: parsed\nprotocol: NEC\naddress: 01 02 03 04\ncommand: 01 02 03 04\n"
        "frequency: 38000\n");
    CHECK(e == IrParseErrFieldForType);
    RUN("name: A\ntype: parsed\nprotocol: NEC\naddress: 01 02 03 04\ncommand: 01 02 03 04\nfoo: 1\n");
    CHECK(e == IrParseErrUnknownKey);
    RUN("name: A\ntype: weird\n");
    CHECK(e == IrParseErrBadType);
    RUN("name: A\ntype: raw\nfrequency: 5\nduty_cycle: 0.3\ndata: 1\n");
    CHECK(e == IrParseErrBadNumber);
    RUN("name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 1.5\ndata: 1\n");
    CHECK(e == IrParseErrBadNumber);
    RUN("name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: 0 5\n");
    CHECK(e == IrParseErrBadNumber);
    RUN("name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: \n");
    CHECK(e == IrParseErrBadNumber);
    RUN("no colon here\n");
    CHECK(e == IrParseErrMalformedLine);
    RUN("name: \n");
    CHECK(e == IrParseErrBadName);

    /* exactly 1024 timings OK, 1025 rejected */
    for(int n = 1024; n <= 1025; n++) {
        char* big = malloc(16384);
        size_t o = (size_t)snprintf(big, 16384, "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata:", hdr);
        for(int i = 0; i < n; i++) o += (size_t)snprintf(big + o, 16384 - o, " 500");
        big[o++] = '\n';
        e = ir_parse_buffer((const uint8_t*)big, o, res);
        if(n == 1024) CHECK(e == IrParseOk && res->signals[0].timings_count == 1024);
        else CHECK(e == IrParseErrTooManyTimings && res->count == 0);
        free(big);
    }

    /* too many signals */
    {
        size_t cap = 200 * (IR_MAX_SIGNALS + 2), o = 0;
        char* s = malloc(cap + 100);
        o += (size_t)snprintf(s, 100, "%s", hdr);
        for(unsigned i = 0; i < IR_MAX_SIGNALS + 1; i++)
            o += (size_t)snprintf(s + o, 200, "#\nname: S%u\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: 1\n", i);
        e = ir_parse_buffer((const uint8_t*)s, o, res);
        CHECK(e == IrParseErrTooManySignals && res->count == 0);
        free(s);
    }

    /* name length bound */
    {
        char nm[200];
        memset(nm, 'x', sizeof(nm));
        nm[IR_MAX_NAME - 1] = 0; /* 63 chars: ok */
        snprintf(buf, sizeof(buf), "%sname: %s\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: 1\n", hdr, nm);
        e = ir_parse_buffer((const uint8_t*)buf, strlen(buf), res);
        CHECK(e == IrParseOk);
        nm[IR_MAX_NAME - 1] = 'x';
        nm[IR_MAX_NAME] = 0; /* 64 chars: rejected */
        snprintf(buf, sizeof(buf), "%sname: %s\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: 1\n", hdr, nm);
        e = ir_parse_buffer((const uint8_t*)buf, strlen(buf), res);
        CHECK(e == IrParseErrBadName);
    }

    /* embedded NUL and control bytes */
    {
        const uint8_t nul[] = "Filetype: IR signals file\nVersion: 1\nname: A\0B\n";
        e = ir_parse_buffer(nul, sizeof(nul) - 1, res);
        CHECK(e == IrParseErrMalformedLine);
    }

    /* line and file bounds with synthetic streams */
    {
        Slow dummy = {0};
        (void)dummy;
        e = ir_parse(endless_read, NULL, res);
        CHECK(e == IrParseErrLineTooLong); /* no newline ever */
        e = ir_parse(failing_read, NULL, res);
        CHECK(e == IrParseErrRead && res->count == 0);
        e = ir_parse(lying_read, NULL, res);
        CHECK(e == IrParseErrRead);
        e = ir_parse(NULL, NULL, res);
        CHECK(e == IrParseErrRead);
        e = ir_parse_buffer(NULL, 100, res);
        CHECK(e == IrParseErrTruncated);
    }
    {
        /* many short lines: exceeds IR_MAX_FILE_BYTES without any long line */
        size_t n = IR_MAX_FILE_BYTES + 1024;
        uint8_t* b = malloc(n);
        const char* h = "Filetype: IR signals file\nVersion: 1\n";
        size_t hl = strlen(h);
        memcpy(b, h, hl);
        for(size_t i = hl; i < n; i++) b[i] = (i % 2) ? '\n' : '#';
        e = ir_parse_buffer(b, n, res);
        CHECK(e == IrParseErrFileTooLarge);
        free(b);
    }
}

static void test_robustness(const char* dir) {
    static const char* files[] = {"valid_parsed.ir", "valid_raw.ir"};
    for(size_t f = 0; f < 2; f++) {
        size_t len;
        uint8_t* b = slurp(dir, files[f], &len);
        if(!b) continue;
        /* every prefix, byte-at-a-time reader */
        for(size_t n = 0; n <= len; n++) {
            Slow s = {b, n, 0};
            IrParseError e = ir_parse(slow_read, &s, res);
            CHECK(res->count <= IR_MAX_SIGNALS);
            if(e != IrParseOk) CHECK(res->count == 0);
        }
        /* byte flips with a deterministic LCG */
        uint32_t x = 12345;
        for(int it = 0; it < 20000; it++) {
            uint8_t* c = malloc(len);
            memcpy(c, b, len);
            for(int k = 0; k < 3; k++) {
                x = x * 1664525u + 1013904223u;
                c[(x >> 8) % len] = (uint8_t)(x >> 24);
            }
            ir_parse_buffer(c, len, res);
            CHECK(res->count <= IR_MAX_SIGNALS);
            free(c);
        }
        /* slow reader must give the same answer as the buffer reader */
        Slow s = {b, len, 0};
        IrParseError e1 = ir_parse(slow_read, &s, res);
        size_t c1 = res->count;
        IrParseError e2 = ir_parse_buffer(b, len, res);
        CHECK(e1 == e2 && c1 == res->count);
        free(b);
    }
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "../corpus";
    res = malloc(sizeof(*res));
    if(!res) return 2;
    test_corpus(dir);
    test_inline();
    test_robustness(dir);
    free(res);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
