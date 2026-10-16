/* PC tests: run from ir_parse/ (CORPUS_DIR defaults to ../corpus). */
#include "ir_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CORPUS_DIR
#define CORPUS_DIR "../corpus"
#endif

static int g_fail = 0;
static int g_checks = 0;

#define CHECK(cond) \
    do { \
        g_checks++; \
        if(!(cond)) { \
            g_fail++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

static char* read_file(const char* name, size_t* len) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", CORPUS_DIR, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("cannot open %s\n", path);
        exit(2);
    }
    size_t cap = 1 << 16, n = 0;
    char* buf = malloc(cap);
    size_t r;
    while((r = fread(buf + n, 1, cap - n, f)) > 0) n += r;
    fclose(f);
    *len = n;
    return buf;
}

static IrParser g_p; /* ~7 KB, keep off the stack */

/* Parse with a given chunk size; chunk 0 = whole buffer. */
static IrStatus parse_chunked(const char* d, size_t n, size_t chunk) {
    ir_parser_init(&g_p);
    if(chunk == 0) chunk = n ? n : 1;
    bool ok = true;
    for(size_t i = 0; i < n && ok; i += chunk) {
        size_t c = n - i < chunk ? n - i : chunk;
        ok = ir_parser_feed(&g_p, d + i, c);
    }
    if(ok) ir_parser_finish(&g_p);
    return g_p.status;
}

static IrStatus parse_corpus(const char* name) {
    size_t n;
    char* d = read_file(name, &n);
    IrStatus s = parse_chunked(d, n, 0);
    /* result must not depend on how the input is chunked */
    static const size_t chunks[] = {1, 3, 64, 4096};
    for(size_t i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        IrStatus s2 = parse_chunked(d, n, chunks[i]);
        if(s2 != s) {
            g_fail++;
            printf("FAIL chunking differs for %s (chunk %zu)\n", name, chunks[i]);
        }
    }
    s = parse_chunked(d, n, 0); /* leave g_p holding the whole-buffer result */
    free(d);
    return s;
}

static void test_valid_parsed(void) {
    CHECK(parse_corpus("valid_parsed.ir") == IrOk);
    CHECK(g_p.count == 2);
    CHECK(strcmp(g_p.buttons[0].name, "Power") == 0);
    CHECK(g_p.buttons[0].type == IrTypeParsed);
    CHECK(strcmp(g_p.buttons[0].protocol, "NEC") == 0);
    CHECK(g_p.buttons[0].address[0] == 0x04 && g_p.buttons[0].address[3] == 0x00);
    CHECK(g_p.buttons[0].command[0] == 0x08);
    CHECK(strcmp(g_p.buttons[1].name, "Vol_up") == 0);
    CHECK(strcmp(g_p.buttons[1].protocol, "NECext") == 0);
    CHECK(g_p.buttons[1].address[0] == 0xEE && g_p.buttons[1].address[1] == 0x87);
    CHECK(g_p.buttons[1].command[0] == 0x5D && g_p.buttons[1].command[1] == 0xA0);
}

static void test_valid_raw(void) {
    CHECK(parse_corpus("valid_raw.ir") == IrOk);
    CHECK(g_p.count == 1);
    CHECK(strcmp(g_p.buttons[0].name, "Fan") == 0);
    CHECK(g_p.buttons[0].type == IrTypeRaw);
    CHECK(g_p.buttons[0].frequency == 38000);
    CHECK(g_p.buttons[0].duty_cycle_ppm == 330000);
    CHECK(g_p.buttons[0].timing_count == 11);
}

static void test_rejects(void) {
    CHECK(parse_corpus("empty.ir") == IrErrFiletype);
    CHECK(parse_corpus("wrong_filetype.ir") == IrErrFiletype);
    CHECK(parse_corpus("future_version.ir") == IrErrVersion);
    CHECK(g_p.count == 0);
    CHECK(parse_corpus("truncated.ir") != IrOk);
    CHECK(g_p.count == 0);
    CHECK(parse_corpus("duplicate_and_short_hex.ir") != IrOk);
    CHECK(parse_corpus("nonprintable_name.ir") == IrErrBadChar);
    CHECK(parse_corpus("overflow_values.ir") == IrErrOverflow);
    CHECK(parse_corpus("too_many_timings.ir") == IrErrTooManyTimings);
    IrStatus s = parse_corpus("long_line.ir");
    CHECK(s == IrErrBadName); /* the 5 KB file also has a name longer than IR_NAME_MAX */
    CHECK(g_p.count == 0);
}

static void test_instruction_in_name_is_plain_data(void) {
    CHECK(parse_corpus("instruction_in_name.ir") == IrOk);
    CHECK(g_p.count == 1);
    CHECK(strncmp(g_p.buttons[0].name, "Ignore previous instructions", 28) == 0);
    for(const char* c = g_p.buttons[0].name; *c; c++) CHECK((unsigned char)*c >= 0x20 && *c < 0x7f);
}

static IrStatus parse_str(const char* s) {
    return parse_chunked(s, strlen(s), 0);
}

#define HDR "Filetype: IR signals file\nVersion: 1\n#\n"

static void test_inline_cases(void) {
    const char* parsed = "name: A\ntype: parsed\nprotocol: NEC\naddress: 01 02 03 04\ncommand: 05 06 07 08\n";
    char buf[1024];

    snprintf(buf, sizeof(buf), HDR "%s", parsed);
    CHECK(parse_str(buf) == IrOk);

    /* CRLF and missing final newline */
    CHECK(parse_str("Filetype: IR signals file\r\nVersion: 1\r\n#\r\nname: A\r\ntype: raw\r\n"
                    "frequency: 38000\r\nduty_cycle: 0.5\r\ndata: 1 2 3") == IrOk);
    CHECK(g_p.buttons[0].timing_count == 3);

    /* unsupported key, duplicate key, key before name, mixed fields */
    snprintf(buf, sizeof(buf), HDR "%sdecoded: yes\n", parsed);
    CHECK(parse_str(buf) == IrErrUnsupportedKey);
    snprintf(buf, sizeof(buf), HDR "%stype: raw\n", parsed);
    CHECK(parse_str(buf) == IrErrDuplicateKey);
    CHECK(parse_str(HDR "type: raw\n") == IrErrKeyBeforeName);
    snprintf(buf, sizeof(buf), HDR "%sfrequency: 38000\n", parsed);
    CHECK(parse_str(buf) == IrErrIncomplete);

    /* header only / header cut */
    CHECK(parse_str(HDR) == IrErrTruncated);
    CHECK(parse_str("Filetype: IR signals file\n") == IrErrVersion);

    /* ranges */
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 9\nduty_cycle: 0.3\ndata: 1\n") == IrErrRange);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 1.5\ndata: 1\n") == IrErrRange);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: 0\n") == IrErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: -5\n") == IrErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata: 99999999999\n") == IrErrOverflow);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata:\n") == IrErrBadValue);

    /* hex strictness */
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00\ncommand: 08 00 00 00\n") == IrErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 0G 00 00 00\ncommand: 08 00 00 00\n") == IrErrBadValue);

    /* name limits */
    CHECK(parse_str(HDR "name:\ntype: raw\n") == IrErrBadName);
    char longname[200];
    memset(longname, 'x', sizeof(longname));
    longname[sizeof(longname) - 1] = 0;
    snprintf(buf, sizeof(buf), HDR "name: %s\n", longname);
    CHECK(parse_str(buf) == IrErrBadName);

    /* control byte and NUL inside a value */
    CHECK(parse_str(HDR "name: A\x01\n") == IrErrBadChar);
    CHECK(parse_chunked(HDR "name: A\0B\ntype: raw\n", sizeof(HDR "name: A\0B\ntype: raw\n") - 1, 0) == IrErrBadChar);
    CHECK(parse_str(HDR "name: caf\xc3\xa9\n") == IrErrBadChar);
}

static void test_limits(void) {
    /* exactly IR_MAX_TIMINGS is accepted, one more is not */
    size_t cap = 256 + IR_MAX_TIMINGS * 2 + 16;
    char* b = malloc(cap + 8);
    size_t n = (size_t)snprintf(
        b, cap, HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.3\ndata:");
    for(unsigned i = 0; i < IR_MAX_TIMINGS; i++) n += (size_t)sprintf(b + n, " 5");
    b[n++] = '\n';
    CHECK(parse_chunked(b, n, 0) == IrOk);
    CHECK(g_p.buttons[0].timing_count == IR_MAX_TIMINGS);
    n--;
    n += (size_t)sprintf(b + n, " 5\n");
    CHECK(parse_chunked(b, n, 0) == IrErrTooManyTimings);
    free(b);

    /* more than IR_MAX_BUTTONS signals */
    size_t cap2 = 128 + (IR_MAX_BUTTONS + 2) * 100;
    char* m = malloc(cap2);
    size_t k = (size_t)sprintf(m, HDR);
    for(unsigned i = 0; i < IR_MAX_BUTTONS + 1; i++)
        k += (size_t)sprintf(
            m + k, "name: B%u\ntype: parsed\nprotocol: NEC\naddress: 01 00 00 00\ncommand: 02 00 00 00\n#\n", i);
    CHECK(parse_chunked(m, k, 0) == IrErrTooManyButtons);
    CHECK(g_p.count == IR_MAX_BUTTONS);
    free(m);

    /* endless line without newline: stops at IR_MAX_LINE, never grows */
    size_t big = IR_MAX_LINE + 100;
    char* l = malloc(big);
    memset(l, ' ', big);
    memcpy(l, "Filetype:", 9);
    CHECK(parse_chunked(l, big, 0) == IrErrLineTooLong);
    free(l);

    /* oversize file of comment lines */
    ir_parser_init(&g_p);
    ir_parser_feed(&g_p, HDR, strlen(HDR));
    char line[64];
    memset(line, 'c', sizeof(line));
    line[0] = '#';
    line[63] = '\n';
    for(unsigned i = 0; i < IR_MAX_FILE / 64 + 4 && g_p.status == IrOk; i++)
        ir_parser_feed(&g_p, line, sizeof(line));
    CHECK(g_p.status == IrErrFileTooLarge);
}

/* Deterministic mutation fuzz; meaningful under -fsanitize=address,undefined. */
static unsigned long long g_rng = 88172645463325252ULL;
static unsigned rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (unsigned)(g_rng >> 11);
}

static void test_fuzz(void) {
    static const char* const files[] = {"valid_parsed.ir", "valid_raw.ir", "duplicate_and_short_hex.ir", "overflow_values.ir"};
    for(size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        size_t n;
        char* orig = read_file(files[f], &n);
        char* d = malloc(n + 16);
        for(int iter = 0; iter < 3000; iter++) {
            size_t len = n;
            memcpy(d, orig, n);
            int muts = 1 + (int)(rnd() % 6);
            while(muts--) {
                switch(rnd() % 4) {
                case 0: d[rnd() % len] = (char)rnd(); break;
                case 1: len = rnd() % (len + 1); if(len == 0) len = 1; break;
                case 2: d[rnd() % len] = "\n: #-09 "[rnd() % 8]; break;
                default: if(len + 1 <= n + 16) { size_t at = rnd() % len; memmove(d + at + 1, d + at, len - at); d[at] = (char)rnd(); len++; }
                }
            }
            parse_chunked(d, len, 1 + rnd() % 50);
            CHECK(g_p.count <= IR_MAX_BUTTONS);
        }
        free(d);
        free(orig);
    }
}

int main(void) {
    test_valid_parsed();
    test_valid_raw();
    test_rejects();
    test_instruction_in_name_is_plain_data();
    test_inline_cases();
    test_limits();
    test_fuzz();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
