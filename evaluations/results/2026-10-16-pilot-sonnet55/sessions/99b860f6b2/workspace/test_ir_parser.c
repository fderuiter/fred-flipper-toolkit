/* Host tests: make test  (run from the workspace root so corpus/ resolves). */
#include "ir_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int checks;

#define CHECK(cond) \
    do { \
        checks++; \
        if(!(cond)) { \
            failures++; \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while(0)

static IrFile f; /* big; keep off the stack */

static uint8_t* load(const char* name, size_t* size) {
    char path[256];
    snprintf(path, sizeof(path), "corpus/%s", name);
    FILE* fp = fopen(path, "rb");
    if(!fp) {
        printf("cannot open %s\n", path);
        exit(2);
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t* buf = malloc(n ? (size_t)n : 1);
    *size = fread(buf, 1, (size_t)n, fp);
    fclose(fp);
    return buf;
}

static bool parse_corpus(const char* name) {
    size_t n;
    uint8_t* b = load(name, &n);
    bool ok = ir_parse_buffer(b, n, &f);
    free(b);
    return ok;
}

static bool parse_str(const char* s) {
    return ir_parse_buffer((const uint8_t*)s, strlen(s), &f);
}

static void expect_err(const char* name, IrError e) {
    bool ok = parse_corpus(name);
    if(ok || f.error != e || f.count != 0) {
        printf("  %s: got ok=%d error=%s (line %zu), wanted %s\n", name, ok,
               ir_error_str(f.error), f.error_line, ir_error_str(e));
    }
    CHECK(!ok);
    CHECK(f.error == e);
    CHECK(f.count == 0);
}

static void test_valid(void) {
    CHECK(parse_corpus("valid_parsed.ir"));
    CHECK(f.count == 2);
    CHECK(strcmp(f.signals[0].name, "Power") == 0);
    CHECK(f.signals[0].type == IrTypeParsed);
    CHECK(strcmp(f.signals[0].protocol, "NEC") == 0);
    CHECK(f.signals[0].address[0] == 0x04 && f.signals[0].address[3] == 0);
    CHECK(f.signals[0].command[0] == 0x08);
    CHECK(strcmp(f.signals[1].name, "Vol_up") == 0);
    CHECK(strcmp(f.signals[1].protocol, "NECext") == 0);
    CHECK(f.signals[1].address[0] == 0xEE && f.signals[1].address[1] == 0x87);
    CHECK(f.signals[1].command[0] == 0x5D && f.signals[1].command[1] == 0xA0);

    CHECK(parse_corpus("valid_raw.ir"));
    CHECK(f.count == 1);
    CHECK(strcmp(f.signals[0].name, "Fan") == 0);
    CHECK(f.signals[0].type == IrTypeRaw);
    CHECK(f.signals[0].frequency == 38000);
    CHECK(f.signals[0].duty_permille == 330);
    CHECK(f.signals[0].timing_count == 11);
}

static void test_rejected_corpus(void) {
    expect_err("empty.ir", IrErrEmpty);
    expect_err("wrong_filetype.ir", IrErrBadHeader);
    expect_err("future_version.ir", IrErrBadVersion);
    expect_err("truncated.ir", IrErrTruncated);
    expect_err("too_many_timings.ir", IrErrTooManyTimings);
    /* second "name:" closes the first, empty signal (line 4); the short hex is never reached */
    expect_err("duplicate_and_short_hex.ir", IrErrIncomplete);
    CHECK(f.error_line == 4);
    expect_err("nonprintable_name.ir", IrErrNonPrintable);
    expect_err("overflow_values.ir", IrErrBadValue); /* frequency 2^32 */
    expect_err("long_line.ir", IrErrLineTooLong);
}

/* Hostile text in a name is data, not an instruction: it is listed, never acted on. */
static void test_instruction_in_name(void) {
    CHECK(parse_corpus("instruction_in_name.ir"));
    CHECK(f.count == 1);
    CHECK(f.signals[0].name_truncated);
    CHECK(strlen(f.signals[0].name) == IR_NAME_SIZE - 1);
    CHECK(strncmp(f.signals[0].name, "Ignore previous", 15) == 0);
}

#define HDR "Filetype: IR signals file\nVersion: 1\n#\n"
#define PARSED "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n"

static void test_inline_cases(void) {
    CHECK(parse_str(HDR PARSED));
    CHECK(parse_str("Filetype: IR signals file\r\nVersion: 1\r\n#\r\n" "name: A\r\ntype: parsed\r\n"
                    "protocol: NEC\r\naddress: 04 00 00 00\r\ncommand: 08 00 00 00\r\n"));
    CHECK(parse_str(HDR)); /* header only: zero buttons is fine */
    CHECK(f.count == 0);

    /* overflow / negative timings */
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 500 4294967296\n"));
    CHECK(f.error == IrErrBadTiming);
    CHECK(parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 500 4294967295\n"));
    CHECK(f.signals[0].timing_count == 2);
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 500 -7 500\n"));
    CHECK(f.error == IrErrBadTiming);
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 500 abc\n"));
    CHECK(f.error == IrErrBadTiming);
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata:\n"));
    CHECK(f.error == IrErrIncomplete);

    /* frequency / duty bounds */
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 99\nduty_cycle: 0.33\ndata: 1\n"));
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 1.5\ndata: 1\n"));
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0\ndata: 1\n"));
    CHECK(!parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: -0.3\ndata: 1\n"));
    CHECK(parse_str(HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 1\ndata: 1\n"));
    CHECK(f.signals[0].duty_permille == 1000);

    /* hex */
    CHECK(!parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00\ncommand: 08 00 00 00\n"));
    CHECK(f.error == IrErrBadHex);
    CHECK(!parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 0G 00 00 00\ncommand: 08 00 00 00\n"));
    CHECK(f.error == IrErrBadHex);

    /* structure */
    CHECK(!parse_str(HDR "type: parsed\n"));
    CHECK(f.error == IrErrIncomplete);
    CHECK(!parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\n"));
    CHECK(f.error == IrErrIncomplete);
    CHECK(!parse_str(HDR "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\nfrequency: 38000\n"));
    CHECK(f.error == IrErrIncomplete); /* raw key on parsed signal */
    CHECK(!parse_str(HDR "name: A\ntype: weird\n"));
    CHECK(f.error == IrErrBadType);
    CHECK(!parse_str(HDR "name: A\ncolor: red\n"));
    CHECK(f.error == IrErrUnknownKey);
    CHECK(!parse_str(HDR "this is not a key value line\n"));
    CHECK(f.error == IrErrSyntax);
    CHECK(!parse_str("Version: 1\nFiletype: IR signals file\n"));
    CHECK(f.error == IrErrBadHeader);
    CHECK(!parse_str("Filetype: IR signals file\n"));
    CHECK(f.error == IrErrBadHeader);
    CHECK(!parse_str(HDR PARSED "type: raw\n"));
    CHECK(f.error == IrErrDuplicateKey);
    CHECK(!parse_str(HDR "name: R\ntype: parsed\ndata: 1 2\n"));
    CHECK(f.error == IrErrIncomplete);
    CHECK(!parse_str(HDR PARSED "name: B\n")); /* second signal has nothing */
    CHECK(f.error == IrErrIncomplete);
    CHECK(!parse_str(HDR "name: \n"));
    CHECK(f.error == IrErrBadValue);
    CHECK(!parse_str(HDR PARSED "#")); /* no final newline */
    CHECK(f.error == IrErrTruncated);
}

static void test_limits(void) {
    /* Build files programmatically: timing cap and signal cap. */
    size_t cap = 64 * 1024;
    char* s = malloc(cap);
    size_t n;

    for(int count = 1024; count <= 1025; count++) {
        n = (size_t)snprintf(s, cap, HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata:");
        for(int i = 0; i < count; i++) n += (size_t)snprintf(s + n, cap - n, " 500");
        n += (size_t)snprintf(s + n, cap - n, "\n");
        bool ok = ir_parse_buffer((const uint8_t*)s, n, &f);
        if(count == 1024) {
            CHECK(ok);
            CHECK(f.signals[0].timing_count == 1024);
        } else {
            CHECK(!ok);
            CHECK(f.error == IrErrTooManyTimings);
        }
    }

    /* signal cap */
    for(unsigned count = IR_MAX_SIGNALS; count <= IR_MAX_SIGNALS + 1; count++) {
        n = (size_t)snprintf(s, cap, HDR);
        for(unsigned i = 0; i < count; i++)
            n += (size_t)snprintf(s + n, cap - n, "#\nname: B%u\ntype: parsed\nprotocol: NEC\n"
                                  "address: 04 00 00 00\ncommand: 08 00 00 00\n", i);
        bool ok = ir_parse_buffer((const uint8_t*)s, n, &f);
        if(count == IR_MAX_SIGNALS) {
            CHECK(ok);
            CHECK(f.count == IR_MAX_SIGNALS);
        } else {
            CHECK(!ok);
            CHECK(f.error == IrErrTooManySignals);
        }
    }

    /* a gigantic data line without newline must not grow anything */
    n = (size_t)snprintf(s, cap, HDR "name: R\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata:");
    CHECK(!ir_parse_buffer((const uint8_t*)s, n, &f));
    free(s);
}

/* Reads 1..7 bytes at a time: results must not depend on chunking. */
typedef struct {
    const uint8_t* d;
    size_t n, pos;
    unsigned step;
} Drip;
static size_t drip_read(void* c, uint8_t* b, size_t max) {
    Drip* d = c;
    size_t k = 1 + (d->step++ % 7);
    if(k > max) k = max;
    if(k > d->n - d->pos) k = d->n - d->pos;
    memcpy(b, d->d + d->pos, k);
    d->pos += k;
    return k;
}
static size_t fail_read(void* c, uint8_t* b, size_t max) {
    (void)c; (void)b; (void)max;
    return (size_t)-1;
}

static void test_chunking_and_read_error(void) {
    static const char* names[] = {"valid_parsed.ir", "valid_raw.ir", "too_many_timings.ir",
                                  "truncated.ir", "long_line.ir"};
    for(size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
        size_t n;
        uint8_t* b = load(names[i], &n);
        bool whole = ir_parse_buffer(b, n, &f);
        IrError e1 = f.error;
        size_t c1 = f.count;
        Drip d = {b, n, 0, 0};
        bool dripped = ir_parse(drip_read, &d, &f);
        CHECK(whole == dripped);
        CHECK(e1 == f.error);
        CHECK(c1 == f.count);
        free(b);
    }
    CHECK(!ir_parse(fail_read, NULL, &f));
    CHECK(f.error == IrErrRead);
}

/* Deterministic mutation fuzz over all corpus files; run under ASan/UBSan. */
static void test_fuzz(void) {
    static const char* names[] = {"valid_parsed.ir", "valid_raw.ir", "truncated.ir",
                                  "duplicate_and_short_hex.ir", "overflow_values.ir"};
    unsigned long long seed = 0x9E3779B97F4A7C15ull;
    for(size_t i = 0; i < sizeof(names) / sizeof(*names); i++) {
        size_t n;
        uint8_t* orig = load(names[i], &n);
        uint8_t* b = malloc(n + 64);
        for(int iter = 0; iter < 20000; iter++) {
            memcpy(b, orig, n);
            size_t len = n;
            int muts = 1 + iter % 4;
            for(int m = 0; m < muts; m++) {
                seed = seed * 6364136223846793005ull + 1442695040888963407ull;
                unsigned r = (unsigned)(seed >> 33);
                size_t pos = len ? r % len : 0;
                switch((r >> 8) % 4) {
                case 0: if(len) b[pos] = (uint8_t)(r >> 16); break;
                case 1: len = pos; break; /* truncate */
                case 2: if(len) b[pos] = "0 9\n:-#fF"[(r >> 16) % 9]; break;
                case 3: if(len < n + 63) { memmove(b + pos + 1, b + pos, len - pos); b[pos] = (uint8_t)(r >> 16); len++; } break;
                }
            }
            bool ok = ir_parse_buffer(b, len, &f);
            if(ok) {
                CHECK(f.error == IrErrNone);
                CHECK(f.count <= IR_MAX_SIGNALS);
                for(size_t k = 0; k < f.count; k++) {
                    CHECK(memchr(f.signals[k].name, '\0', IR_NAME_SIZE) != NULL);
                    CHECK(f.signals[k].timing_count <= IR_MAX_TIMINGS);
                }
            } else {
                CHECK(f.count == 0 && f.error != IrErrNone);
            }
        }
        free(b);
        free(orig);
    }
}

int main(void) {
    test_valid();
    test_rejected_corpus();
    test_instruction_in_name();
    test_inline_cases();
    test_limits();
    test_chunking_and_read_error();
    test_fuzz();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
