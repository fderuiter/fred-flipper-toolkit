/* Host tests for ir_parse. Usage: test_ir_parse [corpus_dir]  (default: corpus) */
#include "../src/ir_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0, checks = 0;

#define CHECK(cond) \
    do { \
        checks++; \
        if(!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

static const char* corpus_dir = "corpus";

/* Loads a file; returns malloc'd buffer (never NULL on success, even if empty). */
static char* load(const char* name, size_t* len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", corpus_dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("FAIL cannot open %s\n", path);
        failures++;
        return NULL;
    }
    size_t cap = 1u << 20;
    char* b = malloc(cap);
    *len = fread(b, 1, cap, f);
    fclose(f);
    return b;
}

static IrFile file; /* static: ~8 KB, keep off the stack as on device */

static IrParseResult parse_corpus(const char* name) {
    size_t len = 0;
    char* b = load(name, &len);
    IrParseResult r = {IrParseErrNullArg, 0};
    if(b) {
        r = ir_parse(b, len, &file);
        free(b);
    }
    return r;
}

static void test_valid_parsed(void) {
    IrParseResult r = parse_corpus("valid_parsed.ir");
    CHECK(r.error == IrParseOk);
    CHECK(file.count == 2);
    CHECK(strcmp(file.signals[0].name, "Power") == 0);
    CHECK(file.signals[0].type == IrSignalParsed);
    CHECK(strcmp(file.signals[0].protocol, "NEC") == 0);
    CHECK(file.signals[0].address[0] == 0x04 && file.signals[0].command[0] == 0x08);
    CHECK(strcmp(file.signals[1].name, "Vol_up") == 0);
    CHECK(strcmp(file.signals[1].protocol, "NECext") == 0);
    CHECK(file.signals[1].address[0] == 0xEE && file.signals[1].address[1] == 0x87);
    CHECK(file.signals[1].command[0] == 0x5D && file.signals[1].command[1] == 0xA0);
}

static void test_valid_raw(void) {
    IrParseResult r = parse_corpus("valid_raw.ir");
    CHECK(r.error == IrParseOk);
    CHECK(file.count == 1);
    CHECK(strcmp(file.signals[0].name, "Fan") == 0);
    CHECK(file.signals[0].type == IrSignalRaw);
    CHECK(file.signals[0].frequency == 38000);
    CHECK(file.signals[0].duty_cycle > 0.329f && file.signals[0].duty_cycle < 0.331f);
    CHECK(file.signals[0].timing_count == 11);
}

static void expect_err(const char* name, IrParseError want) {
    IrParseResult r = parse_corpus(name);
    if(r.error != want) printf("  %s: got '%s' want '%s'\n", name, ir_parse_error_str(r.error), ir_parse_error_str(want));
    CHECK(r.error == want);
}

static void test_rejections(void) {
    expect_err("empty.ir", IrParseErrEmpty);
    expect_err("wrong_filetype.ir", IrParseErrBadHeader);
    expect_err("future_version.ir", IrParseErrBadVersion);
    expect_err("truncated.ir", IrParseErrTruncated);
    expect_err("too_many_timings.ir", IrParseErrTooManyTimings);
    expect_err("duplicate_and_short_hex.ir", IrParseErrDuplicateKey);
    expect_err("overflow_values.ir", IrParseErrBadValue);
    expect_err("nonprintable_name.ir", IrParseErrBadValue);

    /* long_line: rejected either by line cap or timing cap; never accepted. */
    IrParseResult r = parse_corpus("long_line.ir");
    CHECK(r.error == IrParseErrLineTooLong || r.error == IrParseErrTooManyTimings);
}

static void test_instruction_in_name_is_data(void) {
    IrParseResult r = parse_corpus("instruction_in_name.ir");
    CHECK(r.error == IrParseOk);
    CHECK(file.count == 1);
    CHECK(strncmp(file.signals[0].name, "Ignore previous", 15) == 0); /* plain text only */
}

static IrParseResult parse_str(const char* s) {
    return ir_parse(s, strlen(s), &file);
}

#define HDR "Filetype: IR signals file\nVersion: 1\n#\n"

static void test_inline(void) {
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00\n").error == IrParseErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\nfoo: 1\n").error == IrParseErrUnknownKey);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\n").error == IrParseErrIncomplete);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00").error == IrParseErrTruncated);
    CHECK(parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\n#\n").error == IrParseErrIncomplete);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 1 2 3\nprotocol: NEC\n").error == IrParseErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 1.5\ndata: 1 2 3\n").error == IrParseErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.5\ndata: 1 0 3\n").error == IrParseErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 5\nduty_cycle: 0.5\ndata: 1 2\n").error == IrParseErrBadValue);
    CHECK(parse_str(HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.5\ndata: \n").error != IrParseOk);
    CHECK(parse_str("garbage").error == IrParseErrBadHeader);
    CHECK(parse_str("Filetype: IR signals file\n").error == IrParseErrBadVersion);
    CHECK(parse_str(HDR "name A\n").error == IrParseErrSyntax);
    CHECK(parse_str("Filetype: IR signals file\r\nVersion: 1\r\n#\r\nname: A\r\ntype: raw\r\nfrequency: 38000\r\nduty_cycle: 0.5\r\ndata: 1 2\r\n").error == IrParseOk);
    CHECK(file.count == 1 && file.signals[0].timing_count == 2);
    CHECK(ir_parse(NULL, 0, &file).error == IrParseErrEmpty);
    CHECK(ir_parse(NULL, 5, &file).error == IrParseErrNullArg);
    CHECK(ir_parse("x", 1, NULL).error == IrParseErrNullArg);
}

static void test_limits(void) {
    /* Exactly 1024 timings accepted, 1025 rejected. */
    static char big[16384];
    for(int n = 1024; n <= 1025; n++) {
        size_t o = (size_t)snprintf(big, sizeof(big), HDR "name: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.5\ndata:");
        for(int i = 0; i < n; i++) o += (size_t)snprintf(big + o, sizeof(big) - o, " 500");
        o += (size_t)snprintf(big + o, sizeof(big) - o, "\n");
        IrParseResult r = ir_parse(big, o, &file);
        if(n == 1024) {
            CHECK(r.error == IrParseOk && file.signals[0].timing_count == 1024);
        } else {
            CHECK(r.error == IrParseErrTooManyTimings);
        }
    }
    /* More than IR_MAX_SIGNALS signals: rejected, count stays bounded. */
    static char many[200000];
    size_t o = (size_t)snprintf(many, sizeof(many), "Filetype: IR signals file\nVersion: 1\n");
    for(int i = 0; i < (int)IR_MAX_SIGNALS + 2; i++)
        o += (size_t)snprintf(many + o, sizeof(many) - o, "#\nname: S%d\ntype: parsed\nprotocol: NEC\naddress: 01 00 00 00\ncommand: 02 00 00 00\n", i);
    IrParseResult r = ir_parse(many, o, &file);
    CHECK(r.error == IrParseErrTooManySignals && file.count == IR_MAX_SIGNALS);
    /* Oversized file and oversized line. */
    static char huge[IR_MAX_FILE_BYTES + 1];
    memset(huge, 'a', sizeof(huge));
    CHECK(ir_parse(huge, sizeof(huge), &file).error == IrParseErrFileTooLarge);
    CHECK(ir_parse(huge, IR_MAX_LINE_LEN + 1, &file).error == IrParseErrLineTooLong);
}

/* Deterministic mutation pass over every corpus file: must never crash or
 * read out of bounds (run under -fsanitize=address,undefined). */
static void test_mutation(void) {
    static const char* names[] = {"valid_parsed.ir", "valid_raw.ir", "truncated.ir", "duplicate_and_short_hex.ir", "overflow_values.ir"};
    unsigned seed = 12345;
    for(size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
        size_t len = 0;
        char* b = load(names[k], &len);
        if(!b) continue;
        for(size_t cut = 0; cut <= len; cut++) { /* every truncation, exact-size heap buffer */
            char* t = malloc(cut ? cut : 1);
            memcpy(t, b, cut);
            (void)ir_parse(t, cut, &file);
            free(t);
        }
        for(int i = 0; i < 2000; i++) {
            char* t = malloc(len);
            memcpy(t, b, len);
            seed = seed * 1664525u + 1013904223u;
            t[(seed >> 8) % len] = (char)(seed >> 24);
            (void)ir_parse(t, len, &file);
            free(t);
        }
        free(b);
    }
    CHECK(1);
}

int main(int argc, char** argv) {
    if(argc > 1) corpus_dir = argv[1];
    test_valid_parsed();
    test_valid_raw();
    test_rejections();
    test_instruction_in_name_is_data();
    test_inline();
    test_limits();
    test_mutation();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
