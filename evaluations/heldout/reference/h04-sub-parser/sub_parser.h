// Reviewer-side reference solution for FRED held-out task H04. MIT licensed, original.
// Bounded parser for a subset of Flipper Sub-GHz RAW files. Pure C99, no
// allocation: the caller supplies the reader and the result keeps only summary
// values. Viewer only; nothing here transmits.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SUB_MAX_LINE     4096u /* bytes per line, excluding newline */
#define SUB_MAX_TIMINGS  4096u
#define SUB_MAX_DURATION 10000000L /* us */

typedef enum {
    SubOk,
    SubErrEmpty,
    SubErrFiletype,
    SubErrVersion,
    SubErrMissingKey,
    SubErrDuplicateKey,
    SubErrUnknownKey,
    SubErrFrequency,
    SubErrFrequencyBand, /* parsed, but outside 300-348, 387-464, 779-928 MHz */
    SubErrPreset,
    SubErrProtocol,
    SubErrLineTooLong,
    SubErrToken,
    SubErrDuration,
    SubErrSign, /* two consecutive durations with the same sign */
    SubErrTooManyTimings,
    SubErrNoTimings,
    SubErrTruncated,
} SubError;

typedef struct {
    uint32_t frequency_hz;
    char preset[40];
    uint32_t timings;
    uint64_t total_us;
    uint32_t line; /* line number of the first error */
} SubSummary;

/* Reads up to cap bytes into buf; returns bytes read, 0 at end. */
typedef size_t (*SubRead)(void* ctx, char* buf, size_t cap);

SubError sub_parse(SubRead read, void* ctx, SubSummary* out);
const char* sub_error_str(SubError e);
