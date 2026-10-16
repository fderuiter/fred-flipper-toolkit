#pragma once

/* Pure settings codec: no Furi dependencies, host-testable. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IT_FILETYPE        "Interval Timer Settings"
#define IT_VERSION_CURRENT 2
#define IT_MAX_FILE        512 /* larger files are rejected, never truncated */

#define IT_WORK_MIN   5
#define IT_WORK_MAX   600
#define IT_REST_MIN   0
#define IT_REST_MAX   600
#define IT_ROUNDS_MIN 1
#define IT_ROUNDS_MAX 99

typedef struct {
    uint32_t work;
    uint32_t rest;
    uint32_t rounds;
    bool vibrate;
} ItSettings;

typedef enum {
    ItParseOk, /* current version */
    ItParseMigrated, /* older known version, values kept, new fields defaulted */
    ItParseFuture, /* newer version than we understand */
    ItParseCorrupt, /* unparsable, truncated, duplicate/missing key, out of range */
} ItParseResult;

void it_settings_defaults(ItSettings* s);

/* `out` is written only for ItParseOk / ItParseMigrated; otherwise it is left untouched.
 * Policy: whole-file reject (no per-field fallback) so a damaged file is never half-applied.
 * Requires a trailing newline (the writer always emits one) to detect truncation. */
ItParseResult it_settings_parse(const char* buf, size_t len, ItSettings* out);

/* Returns bytes written (excluding NUL), or 0 if it does not fit. */
size_t it_settings_format(const ItSettings* s, char* buf, size_t cap);
