/* Host tests for ir_parser. Usage: test_ir_parser [corpus_dir]  (default: corpus) */
#include "ir_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks, g_failures;

#define CHECK(cond) \
    do { \
        g_checks++; \
        if(!(cond)) { \
            g_failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

static const char* g_dir = "corpus";
static IrParser g_parser;
static IrFile g_file;

static uint8_t* load(const char* name, size_t* len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", g_dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("FAIL cannot open %s\n", path);
        g_failures++;
        *len = 0;
        return NULL;
    }
    size_t cap = 1 << 16, n = 0;
    uint8_t* buf = malloc(cap);
    size_t r;
    while(buf && (r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if(n == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    fclose(f);
    *len = n;
    return buf;
}

static IrError parse_chunked(const uint8_t* d, size_t len, size_t chunk) {
    ir_parser_init(&g_parser, &g_file);
    for(size_t i = 0; i < len; i += chunk) {
        size_t n = len - i < chunk ? len - i : chunk;
        if(ir_parser_feed(&g_parser, d + i, n) != IrOk) return g_parser.error;
    }
    return ir_parser_finish(&g_parser);
}

static IrError parse_str(const char* s) {
    return ir_parse_buffer(&g_parser, &g_file, (const uint8_t*)s, strlen(s));
}

/* Parse corpus file whole and in 1/7/4096-byte chunks; all must agree. */
static IrError parse_corpus(const char* name) {
    size_t len;
    uint8_t* d = load(name, &len);
    IrError e = ir_parse_buffer(&g_parser, &g_file, d, len);
    size_t cnt = g_file.count;
    static const size_t chunks[] = {1, 7, 4096};
    for(size_t i = 0; i < 3; i++) {
        CHECK(parse_chunked(d, len, chunks[i]) == e);
        CHECK(g_file.count == cnt || e != IrOk);
    }
    ir_parse_buffer(&g_parser, &g_file, d, len);
    free(d);
    return e;
}

#define HDR "Filetype: IR signals file\nVersion: 1\n#\n"
#define PARSED "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n"

static void test_valid(void) {
    CHECK(parse_corpus("valid_parsed.ir") == IrOk);
    CHECK(g_file.count == 2);
    CHECK(strcmp(g_file.signals[0].name, "Power") == 0);
    CHECK(g_file.signals[0].type == IrSignalParsed);
    CHECK(strcmp(g_file.signals[0].protocol, "NEC") == 0);
    CHECK(g_file.signals[0].address[0] == 0x04 && g_file.signals[0].command[0] == 0x08);
    CHECK(strcmp(g_file.signals[1].protocol, "NECext") == 0);
    CHECK(g_file.signals[1].address[0] == 0xEE && g_file.signals[1].address[1] == 0x87);
    CHECK(g_file.signals[1].command[0] == 0x5D && g_file.signals[1].command[1] == 0xA0);

    CHECK(parse_corpus("valid_raw.ir") == IrOk);
    CHECK(g_file.count == 1);
    CHECK(strcmp(g_file.signals[0].name, "Fan") == 0);
    CHECK(g_file.signals[0].type == IrSignalRaw);
    CHECK(g_file.signals[0].frequency == 38000);
    CHECK(g_file.signals[0].duty_cycle_ppm == 330000);
    CHECK(g_file.signals[0].timing_count == 11);
}

static void test_corpus_rejects(void) {
    CHECK(parse_corpus("truncated.ir") != IrOk);
    CHECK(parse_corpus("too_many_timings.ir") != IrOk);
    CHECK(parse_corpus("duplicate_and_short_hex.ir") == IrErrDuplicateKey);
    CHECK(parse_corpus("future_version.ir") == IrErrVersion);
    CHECK(parse_corpus("nonprintable_name.ir") == IrErrBadChar);
    CHECK(parse_corpus("overflow_values.ir") == IrErrBadValue);
    CHECK(parse_corpus("wrong_filetype.ir") == IrErrHeader);
    CHECK(parse_corpus("long_line.ir") != IrOk); /* name > 64 chars */
    CHECK(parse_corpus("empty.ir") == IrErrTruncated);
    /* Text in a name is data: accepted verbatim, never interpreted. */
    CHECK(parse_corpus("instruction_in_name.ir") == IrOk);
    CHECK(g_file.count == 1 && strncmp(g_file.signals[0].name, "Ignore previous", 15) == 0);
}

static void test_synthetic(void) {
    CHECK(parse_str(HDR PARSED) == IrOk);
    CHECK(parse_str("Filetype: IR signals file\r\nVersion: 1\r\n#\r\n"
                    "name: A\r\ntype: parsed\r\nprotocol: NEC\r\n"
                    "address: 04 00 00 00\r\ncommand: 08 00 00 00\r\n") == IrOk);
    CHECK(parse_str("Filetype: IR signals file\nVersion: 1\n") == IrOk && g_file.count == 0);
    /* final line without newline */
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\n"
                        "command: 08 00 00 00") == IrOk);
    CHECK(parse_str("Filetype: IR signals file") == IrErrTruncated);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\n") ==
          IrErrMissingKey);
    CHECK(parse_str(HDR PARSED "frequency: 38000\n") == IrErrWrongFields);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\n"
                        "data: 1 2 3\nprotocol: NEC\n") == IrErrWrongFields);
    CHECK(parse_str(HDR "name: A\ntype: weird\n") == IrErrBadValue);
    CHECK(parse_str(HDR "name: A\nfoo: bar\n") == IrErrUnknownKey);
    CHECK(parse_str(HDR "name A\n") == IrErrSyntax);
    CHECK(parse_str(HDR "name:A\n") == IrErrSyntax);
    CHECK(parse_str(HDR "name: \n") == IrErrBadValue);
    CHECK(parse_str(HDR "name: A\tB\n") == IrErrBadChar);
    CHECK(parse_str(HDR "name: caf\xc3\xa9\n") == IrErrBadChar);
    CHECK(parse_str("Filetype: IR signals file\nVersion: 1\nname: A\0B\n") != IrOk);

    /* address / command shape */
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00\n") ==
          IrErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 0G\n") ==
          IrErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00 00\n") ==
          IrErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: N EC\n") == IrErrBadValue);

    /* raw numeric ranges */
#define RAW(f, d, data) \
    HDR "name: A\ntype: raw\nfrequency: " f "\nduty_cycle: " d "\ndata: " data "\n"
    CHECK(parse_str(RAW("38000", "0.33", "1 2")) == IrOk);
    CHECK(parse_str(RAW("38000", "1", "4294967295")) == IrOk);
    CHECK(parse_str(RAW("9999", "0.33", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("56001", "0.33", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("-38000", "0.33", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("4294967296", "0.33", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("99999999999999999999", "0.33", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "0", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "1.5", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "-0.3", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "0.1234567", "1")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "0.33", "")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "0.33", "1 -7")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "0.33", "1 4294967296")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "0.33", "1 0")) == IrErrBadValue);
    CHECK(parse_str(RAW("38000", "0.33", "1 2x")) == IrErrBadValue);
}

static void test_limits(void) {
    /* exactly IR_MAX_TIMINGS ok, one more rejected */
    static char buf[IR_MAX_TIMINGS * 4 + 512];
    for(int extra = 0; extra < 2; extra++) {
        size_t n = (size_t)snprintf(
            buf, sizeof(buf), HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata:");
        for(unsigned i = 0; i < IR_MAX_TIMINGS + (unsigned)extra; i++)
            n += (size_t)snprintf(buf + n, sizeof(buf) - n, " 500");
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "\n");
        IrError e = ir_parse_buffer(&g_parser, &g_file, (uint8_t*)buf, n);
        if(extra == 0) {
            CHECK(e == IrOk && g_file.signals[0].timing_count == IR_MAX_TIMINGS);
        } else {
            CHECK(e == IrErrTooManyTimings);
        }
    }

    /* name length boundary */
    char name[IR_MAX_NAME + 2];
    memset(name, 'x', sizeof(name));
    name[IR_MAX_NAME] = '\0';
    snprintf(buf, sizeof(buf), HDR "name: %s\ntype: parsed\nprotocol: NEC\n"
             "address: 00 00 00 00\ncommand: 00 00 00 00\n", name);
    CHECK(parse_str(buf) == IrOk);
    name[IR_MAX_NAME] = 'x';
    name[IR_MAX_NAME + 1] = '\0';
    snprintf(buf, sizeof(buf), HDR "name: %s\n", name);
    CHECK(parse_str(buf) == IrErrBadValue);

    /* too many signals */
    size_t cap = 100 + IR_MAX_SIGNALS * 2 * sizeof(PARSED);
    char* big = malloc(cap);
    for(int extra = 0; extra < 2; extra++) {
        size_t n = (size_t)snprintf(big, cap, "Filetype: IR signals file\nVersion: 1\n");
        for(unsigned i = 0; i < IR_MAX_SIGNALS + (unsigned)extra; i++)
            n += (size_t)snprintf(big + n, cap - n, "#\n" PARSED);
        IrError e = ir_parse_buffer(&g_parser, &g_file, (uint8_t*)big, n);
        CHECK(extra == 0 ? (e == IrOk && g_file.count == IR_MAX_SIGNALS) : e == IrErrTooManySignals);
    }
    free(big);

    /* line length: never buffers past IR_MAX_LINE */
    size_t n = IR_MAX_LINE + 1000;
    uint8_t* line = malloc(n + 64);
    memcpy(line, HDR "name: ", sizeof(HDR "name: ") - 1);
    size_t off = sizeof(HDR "name: ") - 1;
    memset(line + off, 'A', n);
    CHECK(ir_parse_buffer(&g_parser, &g_file, line, off + n) == IrErrLineTooLong);
    free(line);

    /* total size cap, also with no newline at all */
    size_t huge = IR_MAX_FILE_BYTES + 10;
    uint8_t* h = malloc(huge);
    memset(h, '#', huge);
    CHECK(ir_parse_buffer(&g_parser, &g_file, h, huge) != IrOk);
    memset(h, '\n', huge);
    CHECK(ir_parse_buffer(&g_parser, &g_file, h, huge) != IrOk);
    free(h);

    /* error is sticky */
    ir_parser_init(&g_parser, &g_file);
    CHECK(ir_parser_feed(&g_parser, (const uint8_t*)"junk\n", 5) == IrErrHeader);
    CHECK(ir_parser_feed(&g_parser, (const uint8_t*)HDR, sizeof(HDR) - 1) == IrErrHeader);
    CHECK(ir_parser_finish(&g_parser) == IrErrHeader);
    CHECK(g_parser.error_line == 1);
}

/* Deterministic mutation fuzz: must not crash (run under ASan/UBSan) or hang. */
static void test_fuzz(void) {
    static const char* files[] = {"valid_parsed.ir", "valid_raw.ir", "truncated.ir",
                                  "duplicate_and_short_hex.ir", "overflow_values.ir"};
    uint32_t rng = 12345;
#define NEXT() (rng = rng * 1664525u + 1013904223u)
    for(size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        size_t len;
        uint8_t* d = load(files[f], &len);
        if(!d || len == 0) continue;
        for(int iter = 0; iter < 2000; iter++) {
            uint8_t* m = malloc(len);
            memcpy(m, d, len);
            size_t mlen = len;
            int muts = 1 + (int)(NEXT() >> 29);
            for(int k = 0; k < muts; k++) {
                size_t pos = (NEXT() >> 8) % mlen;
                switch((NEXT() >> 16) % 3) {
                case 0: m[pos] = (uint8_t)(NEXT() >> 24); break;
                case 1: mlen = pos ? pos : 1; break;
                default: m[pos] = " \n:#0F-9"[(NEXT() >> 24) % 8]; break;
                }
            }
            (void)ir_parse_buffer(&g_parser, &g_file, m, mlen);
            CHECK(g_file.count <= IR_MAX_SIGNALS);
            free(m);
        }
        free(d);
    }
    /* pure noise */
    uint8_t noise[4096];
    for(int iter = 0; iter < 500; iter++) {
        for(size_t i = 0; i < sizeof(noise); i++) noise[i] = (uint8_t)(NEXT() >> 24);
        (void)ir_parse_buffer(&g_parser, &g_file, noise, sizeof(noise));
    }
}

int main(int argc, char** argv) {
    if(argc > 1) g_dir = argv[1];
    test_valid();
    test_corpus_rejects();
    test_synthetic();
    test_limits();
    test_fuzz();
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
