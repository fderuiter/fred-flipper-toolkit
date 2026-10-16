// Host test for the H04 reference parser over the synthetic corpus.
// Build: cc -std=c99 -Wall -Wextra -Werror sub_parser.c test_sub_parser.c
// Run:   ./a.out ../../fixtures/h04-sub-parser/corpus
#include "sub_parser.h"
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if(!(c)) {                                                                \
            failures++;                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while(0)

static size_t file_read(void* ctx, char* buf, size_t cap) {
    return fread(buf, 1, cap, (FILE*)ctx);
}

static SubError parse(const char* dir, const char* name, SubSummary* s) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "cannot open %s\n", path);
        failures++;
        return SubErrEmpty;
    }
    SubError e = sub_parse(file_read, f, s);
    fclose(f);
    printf("%-22s %s\n", name, sub_error_str(e));
    return e;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "../../fixtures/h04-sub-parser/corpus";
    SubSummary s;
    CHECK(parse(dir, "valid_short.sub", &s) == SubOk);
    CHECK(s.frequency_hz == 433920000u && s.timings == 18 && s.total_us == 18150u);
    CHECK(strcmp(s.preset, "FuriHalSubGhzPresetOok650Async") == 0);
    CHECK(parse(dir, "valid_comment.sub", &s) == SubOk);
    CHECK(s.frequency_hz == 315000000u && s.timings == 12 && s.total_us == 13500u);
    CHECK(parse(dir, "wrong_filetype.sub", &s) == SubErrFiletype);
    CHECK(parse(dir, "future_version.sub", &s) == SubErrVersion);
    CHECK(parse(dir, "not_raw.sub", &s) == SubErrProtocol);
    CHECK(parse(dir, "custom_preset.sub", &s) == SubErrPreset);
    CHECK(parse(dir, "overflow_freq.sub", &s) == SubErrFrequency);
    CHECK(parse(dir, "zero_and_huge.sub", &s) == SubErrDuration);
    CHECK(parse(dir, "non_alternating.sub", &s) == SubErrSign);
    CHECK(parse(dir, "too_many_timings.sub", &s) == SubErrTooManyTimings);
    CHECK(parse(dir, "long_line.sub", &s) == SubErrLineTooLong);
    CHECK(parse(dir, "missing_raw_data.sub", &s) == SubErrNoTimings);
    CHECK(parse(dir, "empty.sub", &s) == SubErrEmpty);
    CHECK(parse(dir, "garbage_token.sub", &s) == SubErrToken);
    CHECK(parse(dir, "out_of_band.sub", &s) == SubErrFrequencyBand);
    CHECK(parse(dir, "truncated.sub", &s) == SubErrTruncated);
    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
