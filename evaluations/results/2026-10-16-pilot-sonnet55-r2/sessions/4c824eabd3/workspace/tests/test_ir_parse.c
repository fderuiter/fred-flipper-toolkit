// PC tests for ir_parse. Run from the project root: `make test`.
#include "../src/ir_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond, ...) \
    do { \
        checks++; \
        if(!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } while(0)

static IrFile file; // static: IrFile is large

static char* slurp(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    size_t cap = 1 << 16, n = 0;
    char* buf = malloc(cap);
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

static IrError parse_file(const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "corpus/%s", name);
    size_t len;
    char* buf = slurp(path, &len);
    CHECK(buf != NULL, "cannot read %s (run from project root)", path);
    if(!buf) return IrErrBadLine;
    IrError e = ir_parse(buf, len, &file);
    free(buf);
    return e;
}

static void expect_err(const char* name, IrError want) {
    IrError got = parse_file(name);
    CHECK(got == want, "%s: want '%s', got '%s' (line %zu)", name, ir_error_str(want),
          ir_error_str(got), file.error_line);
}

static void test_valid(void) {
    CHECK(parse_file("valid_parsed.ir") == IrOk, "valid_parsed rejected");
    CHECK(file.count == 2, "count=%zu", file.count);
    CHECK(strcmp(file.signals[0].name, "Power") == 0, "name0");
    CHECK(file.signals[0].type == IrSignalParsed, "type0");
    CHECK(strcmp(file.signals[0].protocol, "NEC") == 0, "proto0");
    CHECK(file.signals[0].address == 0x04, "addr0=%x", (unsigned)file.signals[0].address);
    CHECK(file.signals[0].command == 0x08, "cmd0");
    CHECK(strcmp(file.signals[1].protocol, "NECext") == 0, "proto1");
    CHECK(file.signals[1].address == 0x87EE, "addr1=%x", (unsigned)file.signals[1].address);
    CHECK(file.signals[1].command == 0xA05D, "cmd1=%x", (unsigned)file.signals[1].command);

    CHECK(parse_file("valid_raw.ir") == IrOk, "valid_raw rejected");
    CHECK(file.count == 1, "raw count");
    CHECK(strcmp(file.signals[0].name, "Fan") == 0, "raw name");
    CHECK(file.signals[0].type == IrSignalRaw, "raw type");
    CHECK(file.signals[0].frequency == 38000, "freq");
    CHECK(file.signals[0].duty_cycle > 0.329f && file.signals[0].duty_cycle < 0.331f, "duty");
    CHECK(file.signals[0].timings_count == 11, "timings=%u", (unsigned)file.signals[0].timings_count);
}

static void test_corpus_rejects(void) {
    expect_err("empty.ir", IrErrEmpty);
    expect_err("truncated.ir", IrErrBadValue); // cut mid-address
    expect_err("too_many_timings.ir", IrErrTooManyTimings);
    expect_err("duplicate_and_short_hex.ir", IrErrDuplicateKey);
    expect_err("future_version.ir", IrErrBadVersion);
    expect_err("nonprintable_name.ir", IrErrBadValue);
    expect_err("overflow_values.ir", IrErrBadValue);
    expect_err("wrong_filetype.ir", IrErrBadHeader);
    expect_err("long_line.ir", IrErrBadValue); // 5000-char name: over limit
    // Instruction-like text in a name is just data: stored verbatim, never interpreted.
    expect_err("instruction_in_name.ir", IrOk);
    CHECK(file.count == 1 && strncmp(file.signals[0].name, "Ignore previous", 15) == 0,
          "instruction name kept as plain data");
}

static void test_inline(void) {
    const char* hdr = "Filetype: IR signals file\nVersion: 1\n#\n";
    char buf[512];

    // CRLF files and a missing trailing newline are accepted.
    const char* crlf = "Filetype: IR signals file\r\nVersion: 1\r\n#\r\nname: A\r\ntype: parsed\r\n"
                       "protocol: NEC\r\naddress: 01 00 00 00\r\ncommand: 02 00 00 00";
    CHECK(ir_parse(crlf, strlen(crlf), &file) == IrOk && file.count == 1, "crlf");

    // Short hex is rejected.
    snprintf(buf, sizeof(buf), "%sname: A\ntype: parsed\nprotocol: NEC\naddress: 04 00\ncommand: 08 00 00 00\n", hdr);
    CHECK(ir_parse(buf, strlen(buf), &file) == IrErrBadValue, "short hex");

    // Unsupported key.
    snprintf(buf, sizeof(buf), "%sname: A\ntype: parsed\nprotocol: NEC\nfoo: 1\n", hdr);
    CHECK(ir_parse(buf, strlen(buf), &file) == IrErrUnknownKey, "unknown key");

    // Missing required key.
    snprintf(buf, sizeof(buf), "%sname: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\n", hdr);
    CHECK(ir_parse(buf, strlen(buf), &file) == IrErrIncomplete, "missing command");

    // Negative / zero timing, trailing space, bad duty.
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 500 -7\n", hdr);
    CHECK(ir_parse(buf, strlen(buf), &file) == IrErrBadValue, "negative timing");
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 500 0\n", hdr);
    CHECK(ir_parse(buf, strlen(buf), &file) == IrErrBadValue, "zero timing");
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 1.5\ndata: 500\n", hdr);
    CHECK(ir_parse(buf, strlen(buf), &file) == IrErrBadValue, "duty > 1");

    // NUL byte inside a line.
    const char nul[] = "Filetype: IR signals file\nVersion: 1\n#\nname: A\0B\n";
    CHECK(ir_parse(nul, sizeof(nul) - 1, &file) == IrErrBadLine, "NUL");

    // NULL buffer with non-zero length does not crash.
    CHECK(ir_parse(NULL, 5, &file) != IrOk, "NULL data");

    // Too large input is refused without scanning.
    char* big = calloc(IR_MAX_FILE_SIZE + 1, 1);
    CHECK(ir_parse(big, IR_MAX_FILE_SIZE + 1, &file) == IrErrTooLarge, "too large");
    free(big);

    // Too many signals: bounded output.
    size_t cap = 200 * 120 + 100;
    char* many = malloc(cap);
    size_t n = (size_t)snprintf(many, cap, "%s", hdr);
    for(int i = 0; i < 200; i++)
        n += (size_t)snprintf(many + n, cap - n, "name: B%d\ntype: parsed\nprotocol: NEC\naddress: 01 00 00 00\ncommand: 01 00 00 00\n#\n", i);
    CHECK(ir_parse(many, n, &file) == IrErrTooManySignals, "too many signals");
    CHECK(file.count == IR_MAX_SIGNALS, "count capped at %zu", file.count);
    free(many);
}

// Every prefix of every corpus file, plus random byte mutations: must return, not crash.
// Run under `make asan` to catch out-of-bounds access.
static void test_robustness(void) {
    static const char* names[] = {"valid_parsed.ir", "valid_raw.ir", "truncated.ir",
                                  "duplicate_and_short_hex.ir", "overflow_values.ir",
                                  "too_many_timings.ir", "long_line.ir"};
    unsigned seed = 12345;
    for(size_t f = 0; f < sizeof(names) / sizeof(names[0]); f++) {
        char path[256];
        snprintf(path, sizeof(path), "corpus/%s", names[f]);
        size_t len;
        char* buf = slurp(path, &len);
        if(!buf) continue;
        // Copy to an exact-size heap block so ASan sees any over-read.
        for(size_t cut = 0; cut <= len; cut++) {
            char* exact = malloc(cut ? cut : 1);
            memcpy(exact, buf, cut);
            (void)ir_parse(exact, cut, &file);
            free(exact);
        }
        for(int it = 0; it < 2000; it++) {
            char* exact = malloc(len ? len : 1);
            memcpy(exact, buf, len);
            for(int m = 0; m < 4 && len; m++) {
                seed = seed * 1103515245u + 12345u;
                exact[(seed >> 8) % len] = (char)(seed >> 20);
            }
            (void)ir_parse(exact, len, &file);
            free(exact);
        }
        free(buf);
    }
    CHECK(1, "robustness run completed");
}

int main(void) {
    test_valid();
    test_corpus_rejects();
    test_inline();
    test_robustness();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
