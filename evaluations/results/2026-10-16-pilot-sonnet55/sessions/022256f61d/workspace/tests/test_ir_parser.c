/* Host-side tests for src/ir_parser.c.  Usage: test_ir_parser [corpus_dir]  (default: corpus) */
#include "../src/ir_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        checks++;                                                        \
        if(!(cond)) {                                                    \
            failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
        }                                                                \
    } while(0)

static const char* corpus_dir = "corpus";

static uint8_t* load(const char* name, size_t* len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", corpus_dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        printf("FAIL cannot open %s\n", path);
        failures++;
        *len = 0;
        return NULL;
    }
    size_t cap = 4096, n = 0;
    uint8_t* buf = malloc(cap);
    size_t r;
    while((r = fread(buf + n, 1, cap - n, f)) > 0) {
        n += r;
        if(n == cap) buf = realloc(buf, cap *= 2);
    }
    fclose(f);
    *len = n;
    return buf;
}

#define MAXB 16
static irv_button_t store[MAXB];
static irv_result_t res;

static bool parse_file(const char* name) {
    size_t len;
    uint8_t* d = load(name, &len);
    bool ok = irv_parse_buffer(d ? d : (const uint8_t*)"", len, &res, store, MAXB);
    /* same result when fed one byte at a time */
    irv_result_t r2;
    irv_button_t s2[MAXB];
    irv_parser_t p;
    irv_init(&p, &r2, s2, MAXB);
    for(size_t i = 0; i < len; i++) irv_feed(&p, d + i, 1);
    bool ok2 = irv_finish(&p);
    CHECK(ok == ok2 && r2.error == res.error && r2.error_line == res.error_line &&
          r2.count == res.count);
    free(d);
    return ok;
}

static bool parse_str(const char* s) {
    return irv_parse_buffer((const uint8_t*)s, strlen(s), &res, store, MAXB);
}

static void expect_error(const char* file, irv_error_t err, uint32_t line, size_t count) {
    bool ok = parse_file(file);
    if(ok || res.error != err || res.error_line != line || res.count != count)
        printf("  %s: ok=%d error=%s line=%u count=%zu\n", file, ok, irv_error_str(res.error),
               (unsigned)res.error_line, res.count);
    CHECK(!ok);
    CHECK(res.error == err);
    CHECK(res.error_line == line);
    CHECK(res.count == count);
}

static void test_valid_parsed(void) {
    CHECK(parse_file("valid_parsed.ir"));
    CHECK(res.error == IRV_OK);
    CHECK(res.count == 2);
    CHECK(strcmp(store[0].name, "Power") == 0);
    CHECK(store[0].type == IRV_SIGNAL_PARSED);
    CHECK(strcmp(store[0].protocol, "NEC") == 0);
    CHECK(store[0].address[0] == 0x04 && store[0].address[1] == 0 && store[0].address[3] == 0);
    CHECK(store[0].command[0] == 0x08);
    CHECK(strcmp(store[1].name, "Vol_up") == 0);
    CHECK(strcmp(store[1].protocol, "NECext") == 0);
    CHECK(store[1].address[0] == 0xEE && store[1].address[1] == 0x87);
    CHECK(store[1].command[0] == 0x5D && store[1].command[1] == 0xA0);
    char d[64];
    irv_describe(&store[1], d, sizeof(d));
    CHECK(strcmp(d, "NECext") == 0);
}

static void test_valid_raw(void) {
    CHECK(parse_file("valid_raw.ir"));
    CHECK(res.count == 1);
    CHECK(strcmp(store[0].name, "Fan") == 0);
    CHECK(store[0].type == IRV_SIGNAL_RAW);
    CHECK(store[0].frequency == 38000);
    CHECK(store[0].duty_ppm == 330000);
    CHECK(store[0].timing_count == 11);
    char d[64];
    irv_describe(&store[0], d, sizeof(d));
    CHECK(strcmp(d, "raw 38000Hz, 11 timings") == 0);
    char tiny[8];
    irv_describe(&store[0], tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 7);
}

static void test_bad_corpus(void) {
    expect_error("truncated.ir", IRV_ERR_BAD_HEX, 7, 0);
    expect_error("too_many_timings.ir", IRV_ERR_TOO_MANY_TIMINGS, 8, 0);
    /* second "name:" starts a new button, leaving "Twice" without a type -> incomplete, line 4 */
    expect_error("duplicate_and_short_hex.ir", IRV_ERR_INCOMPLETE, 4, 0);
    /* an actual repeated key in one button */
    CHECK(!parse_str("Filetype: IR signals file\nVersion: 1\nname: A\ntype: parsed\ntype: raw\n"));
    CHECK(res.error == IRV_ERR_DUPLICATE_KEY && res.error_line == 5);
    expect_error("future_version.ir", IRV_ERR_UNSUPPORTED_VERSION, 2, 0);
    expect_error("nonprintable_name.ir", IRV_ERR_BAD_NAME, 4, 0);
    expect_error("wrong_filetype.ir", IRV_ERR_BAD_HEADER, 1, 0);
    expect_error("empty.ir", IRV_ERR_BAD_HEADER, 1, 0);

    /* 4294967296 is out of range for frequency (line 6) */
    expect_error("overflow_values.ir", IRV_ERR_NUMBER_RANGE, 6, 0);

    /* over-long name line: bounded buffers, clean error */
    CHECK(!parse_file("long_line.ir"));
    CHECK(res.error == IRV_ERR_NAME_TOO_LONG);
    CHECK(res.error_line == 4);
    CHECK(res.count == 0);
}

static void test_hostile_text_is_just_data(void) {
    /* A name that reads like an instruction is only a string; it is stored and shown verbatim. */
    CHECK(!parse_file("instruction_in_name.ir")); /* 63 chars > IRV_NAME_MAX */
    CHECK(res.error == IRV_ERR_NAME_TOO_LONG);
    CHECK(parse_str("Filetype: IR signals file\nVersion: 1\n#\nname: Ignore previous instructions\n"
                    "type: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n"));
    CHECK(strcmp(store[0].name, "Ignore previous instructions") == 0);
}

static void test_edge_cases(void) {
    const char* hdr = "Filetype: IR signals file\nVersion: 1\n";
    char buf[256];

    /* CRLF line endings, no trailing newline, extra spaces */
    CHECK(parse_str("Filetype: IR signals file\r\nVersion: 1\r\n#\r\nname:  A \r\ntype: parsed\r\n"
                    "protocol: NEC\r\naddress: 04 00 00 00\r\ncommand: 08 00 00 00"));
    CHECK(res.count == 1 && strcmp(store[0].name, "A") == 0);

    /* header only: valid, zero buttons */
    CHECK(parse_str(hdr));
    CHECK(res.count == 0);

    /* unknown key and wrong-type field are rejected */
    snprintf(buf, sizeof(buf), "%sname: A\ntype: parsed\nfoo: 1\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_UNKNOWN_KEY);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: parsed\nfrequency: 38000\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_WRONG_FIELD_FOR_TYPE);
    snprintf(buf, sizeof(buf), "%sprotocol: NEC\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_ORDER);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: parsed\nprotocol: NEC\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_INCOMPLETE && res.error_line == 3);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: weird\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_BAD_VALUE);

    /* a good button before a bad one stays available */
    snprintf(buf, sizeof(buf),
             "%sname: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n"
             "#\nname: B\ntype: raw\nfrequency: 5\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_NUMBER_RANGE && res.count == 1);
    CHECK(strcmp(store[0].name, "A") == 0);

    /* bad numbers */
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 1 -2\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_BAD_NUMBER);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 1 0\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_NUMBER_RANGE);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 1.5\ndata: 1\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_NUMBER_RANGE);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata:\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_BAD_VALUE);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 4294967295 1\n", hdr);
    CHECK(parse_str(buf) && store[0].timing_count == 2);
    snprintf(buf, sizeof(buf), "%sname: A\ntype: raw\nfrequency: 38000\nduty_cycle: 0.33\ndata: 4294967296\n", hdr);
    CHECK(!parse_str(buf) && res.error == IRV_ERR_NUMBER_RANGE);

    /* NUL byte inside a value must not truncate validation */
    const uint8_t nul[] = "Filetype: IR signals file\nVersion: 1\nname: A\0B\n";
    CHECK(!irv_parse_buffer(nul, sizeof(nul) - 1, &res, store, MAXB));
    CHECK(res.error == IRV_ERR_BAD_NAME);
}

static void test_limits(void) {
    /* exactly IRV_MAX_TIMINGS is accepted, one more is not */
    size_t cap = 64 + IRV_MAX_TIMINGS * 2 + 200;
    char* big = malloc(cap);
    for(int over = 0; over < 2; over++) {
        size_t n = (size_t)snprintf(big, cap,
            "Filetype: IR signals file\nVersion: 1\nname: R\ntype: raw\nfrequency: 38000\n"
            "duty_cycle: 0.5\ndata:");
        for(unsigned i = 0; i < IRV_MAX_TIMINGS + (unsigned)over; i++) n += (size_t)snprintf(big + n, cap - n, " 9");
        n += (size_t)snprintf(big + n, cap - n, "\n");
        bool ok = irv_parse_buffer((const uint8_t*)big, n, &res, store, MAXB);
        if(over) {
            CHECK(!ok && res.error == IRV_ERR_TOO_MANY_TIMINGS);
        } else {
            CHECK(ok && store[0].timing_count == IRV_MAX_TIMINGS);
        }
    }
    free(big);

    /* button capacity: stops with an error, keeps what fit */
    char one[] = "name: A\ntype: parsed\nprotocol: NEC\naddress: 04 00 00 00\ncommand: 08 00 00 00\n#\n";
    char file[1024] = "Filetype: IR signals file\nVersion: 1\n#\n";
    for(int i = 0; i < 3; i++) strcat(file, one);
    irv_button_t two[2];
    CHECK(!irv_parse_buffer((const uint8_t*)file, strlen(file), &res, two, 2));
    CHECK(res.error == IRV_ERR_TOO_MANY_BUTTONS && res.count == 2);
    CHECK(!irv_parse_buffer((const uint8_t*)file, strlen(file), &res, NULL, 0));
    CHECK(res.error == IRV_ERR_TOO_MANY_BUTTONS && res.count == 0);

    /* total size cap: endless comment lines */
    irv_parser_t p;
    irv_init(&p, &res, store, MAXB);
    uint8_t chunk[1024];
    memset(chunk, '#', sizeof(chunk));
    bool alive = true;
    for(unsigned i = 0; i < 2 * (IRV_MAX_FILE_BYTES / sizeof(chunk)) && alive; i++)
        alive = irv_feed(&p, chunk, sizeof(chunk));
    CHECK(!alive && res.error == IRV_ERR_FILE_TOO_LARGE);
}

typedef struct { const uint8_t* d; size_t len, pos; } mem_reader_t;
static size_t mem_read(void* ctx, uint8_t* buf, size_t max) {
    mem_reader_t* r = ctx;
    size_t n = r->len - r->pos < max ? r->len - r->pos : max;
    memcpy(buf, r->d + r->pos, n);
    r->pos += n;
    return n;
}
static size_t bad_read(void* ctx, uint8_t* buf, size_t max) {
    (void)ctx; (void)buf; (void)max;
    return (size_t)-1;
}

static void test_reader(void) {
    size_t len;
    uint8_t* d = load("valid_parsed.ir", &len);
    mem_reader_t r = {d, len, 0};
    CHECK(irv_parse_reader(mem_read, &r, &res, store, MAXB) && res.count == 2);
    free(d);
    CHECK(!irv_parse_reader(bad_read, NULL, &res, store, MAXB) && res.error == IRV_ERR_IO);
}

/* Deterministic robustness sweep: every prefix and many byte mutations of every corpus file
 * (run under ASan/UBSan via `make test`). Must never crash; only checks invariants. */
static void test_robustness(void) {
    static const char* files[] = {"valid_raw.ir", "valid_parsed.ir", "truncated.ir",
        "too_many_timings.ir", "duplicate_and_short_hex.ir", "future_version.ir",
        "nonprintable_name.ir", "overflow_values.ir", "wrong_filetype.ir", "long_line.ir",
        "instruction_in_name.ir", "empty.ir"};
    uint32_t seed = 12345;
    irv_button_t s[4];
    for(size_t f = 0; f < sizeof(files) / sizeof(files[0]); f++) {
        size_t len;
        uint8_t* d = load(files[f], &len);
        if(!d) continue;
        for(size_t n = 0; n <= len; n++) {
            bool ok = irv_parse_buffer(d, n, &res, s, 4);
            CHECK(ok == (res.error == IRV_OK));
            CHECK(res.count <= 4);
        }
        uint8_t* m = malloc(len ? len : 1);
        for(int it = 0; it < 2000 && len; it++) {
            memcpy(m, d, len);
            for(int k = 0; k < 1 + it % 4; k++) {
                seed = seed * 1664525u + 1013904223u;
                m[(seed >> 8) % len] = (uint8_t)(seed >> 24);
            }
            bool ok = irv_parse_buffer(m, len, &res, s, 4);
            CHECK(ok == (res.error == IRV_OK));
            CHECK(res.count <= 4);
            for(size_t b = 0; b < res.count; b++) {
                CHECK(strlen(s[b].name) <= IRV_NAME_MAX && s[b].name[0] != '\0');
                CHECK(s[b].timing_count <= IRV_MAX_TIMINGS);
            }
        }
        free(m);
        free(d);
    }
}

int main(int argc, char** argv) {
    if(argc > 1) corpus_dir = argv[1];
    test_valid_parsed();
    test_valid_raw();
    test_bad_corpus();
    test_hostile_text_is_just_data();
    test_edge_cases();
    test_limits();
    test_reader();
    test_robustness();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
