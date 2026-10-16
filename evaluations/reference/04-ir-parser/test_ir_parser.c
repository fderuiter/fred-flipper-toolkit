// Host test for the task 04 reference parser over the fixture corpus.
// Build: cc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined ir_parser.c test_ir_parser.c
// Run:   ./a.out ../../fixtures/04-ir-parser/corpus
#include "ir_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char* file;
    IrError expect;
    uint32_t signals; /* checked only when expect == IrOk */
    uint32_t first_timings;
} Case;

static const Case cases[] = {
    {"valid_parsed.ir", IrOk, 2, 0},
    {"valid_raw.ir", IrOk, 1, 11},
    {"truncated.ir", IrErrHex, 0, 0},
    {"too_many_timings.ir", IrErrTooManyTimings, 0, 0},
    {"future_version.ir", IrErrVersion, 0, 0},
    {"wrong_filetype.ir", IrErrHeader, 0, 0},
    {"overflow_values.ir", IrErrNumber, 0, 0},
    {"duplicate_and_short_hex.ir", IrErrDuplicateKey, 0, 0},
    {"nonprintable_name.ir", IrErrName, 0, 0},
    {"long_line.ir", IrErrLineTooLong, 0, 0},
    {"instruction_in_name.ir", IrErrName, 0, 0},
    {"empty.ir", IrErrHeader, 0, 0},
};

static char* slurp(const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    char* buf = malloc(IR_MAX_FILE_BYTES + 2);
    size_t n = buf ? fread(buf, 1, IR_MAX_FILE_BYTES + 1, f) : 0;
    fclose(f);
    *len = n;
    return buf;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : ".";
    static IrRemote remote;
    int failures = 0;
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, cases[i].file);
        size_t len = 0;
        char* data = slurp(path, &len);
        if(!data) {
            printf("FAIL %s: cannot read\n", cases[i].file);
            failures++;
            continue;
        }
        IrResult r = ir_parse(data, len, &remote);
        int ok = r.code == cases[i].expect;
        if(ok && r.code == IrOk) {
            ok = remote.signal_count == cases[i].signals &&
                 (cases[i].first_timings == 0 ||
                  remote.signals[0].timing_count == cases[i].first_timings);
        }
        printf(
            "%s %-28s -> %s (line %u)\n",
            ok ? "PASS" : "FAIL",
            cases[i].file,
            ir_error_name(r.code),
            (unsigned)r.line);
        failures += !ok;
        free(data);
    }
    /* Truncation sweep: every prefix of the valid files must parse or fail cleanly. */
    const char* sweep[] = {"valid_parsed.ir", "valid_raw.ir"};
    for(size_t s = 0; s < 2; s++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, sweep[s]);
        size_t len = 0;
        char* data = slurp(path, &len);
        if(!data) return 1;
        for(size_t n = 0; n <= len; n++) {
            char* copy = malloc(n ? n : 1);
            memcpy(copy, data, n);
            (void)ir_parse(copy, n, &remote); /* ASan/UBSan catch out-of-bounds */
            free(copy);
        }
        printf("PASS prefix sweep %s (%zu prefixes)\n", sweep[s], len + 1);
        free(data);
    }
    /* Boundary: exactly IR_MAX_TIMINGS maximal values is accepted. */
    {
        static char buf[IR_MAX_FILE_BYTES];
        int n = snprintf(
            buf,
            sizeof(buf),
            "Filetype: IR signals file\nVersion: 1\n#\nname: Max\ntype: raw\n"
            "frequency: 38000\nduty_cycle: 0.33\ndata:");
        for(unsigned i = 0; i < IR_MAX_TIMINGS; i++)
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, " 4294967295");
        IrResult r = ir_parse(buf, (size_t)n, &remote);
        int ok = r.code == IrOk && remote.signals[0].timing_count == IR_MAX_TIMINGS;
        printf(
            "%s boundary %u maximal timings -> %s\n",
            ok ? "PASS" : "FAIL",
            IR_MAX_TIMINGS,
            ir_error_name(r.code));
        failures += !ok;
    }
    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
