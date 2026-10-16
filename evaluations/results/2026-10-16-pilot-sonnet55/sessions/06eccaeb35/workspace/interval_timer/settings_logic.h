#pragma once

/* Pure settings logic: no Furi dependencies, host-testable.
 * File text is FlipperFormat-compatible ("Key: value" lines). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IT_FILETYPE          "Interval Timer Settings"
#define IT_FORMAT_VERSION    2
#define IT_SETTINGS_MAX_BYTES 512
#define IT_SETTINGS_MAX_LINE  64

#define IT_WORK_MIN   5
#define IT_WORK_MAX   600
#define IT_REST_MIN   0
#define IT_REST_MAX   600
#define IT_ROUNDS_MIN 1
#define IT_ROUNDS_MAX 99

#define IT_WORK_DEFAULT    30
#define IT_REST_DEFAULT    10
#define IT_ROUNDS_DEFAULT  8
#define IT_VIBRATE_DEFAULT true

typedef struct {
    uint16_t work;
    uint16_t rest;
    uint8_t rounds;
    bool vibrate;
} ITSettings;

typedef enum {
    ITLoadOk, /* v2, every field valid */
    ITLoadMigratedV1, /* v1: Work/Rest kept, Rounds/Vibrate defaulted */
    ITLoadRepaired, /* known version, some field missing/invalid -> that field defaulted */
    ITLoadCorrupt, /* header unusable -> all defaults, file may be replaced */
    ITLoadFuture, /* version > 2: known fields used for this session, file must NOT be overwritten */
    ITLoadTooLarge, /* more than IT_SETTINGS_MAX_BYTES: defaults, file must NOT be overwritten */
} ITLoadStatus;

void it_settings_defaults(ITSettings* s);

/* Always fills *out (defaults for anything not recovered). Duplicate keys: last wins.
 * Unknown keys and over-long lines are ignored. */
ITLoadStatus it_settings_parse(const char* buf, size_t len, ITSettings* out);

/* Writes a v2 file. Returns bytes written (excluding NUL), or 0 if it does not fit
 * or the settings are out of range. */
size_t it_settings_format(const ITSettings* s, char* out, size_t cap);

/* True when the existing file must be left untouched. */
bool it_load_status_is_readonly(ITLoadStatus st);
