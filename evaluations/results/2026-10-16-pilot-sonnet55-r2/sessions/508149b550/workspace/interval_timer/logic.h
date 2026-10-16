#pragma once
/* Pure logic: no Furi dependencies, so it can be host-tested. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_FILETYPE        "Interval Timer Settings"
#define SETTINGS_VERSION         2
#define SETTINGS_MAX_FILE_BYTES  512
#define SETTINGS_MAX_LINE_BYTES  64

#define WORK_MIN   5
#define WORK_MAX   600
#define REST_MIN   0
#define REST_MAX   600
#define ROUNDS_MIN 1
#define ROUNDS_MAX 99

typedef struct {
    uint16_t work;
    uint16_t rest;
    uint16_t rounds;
    bool vibrate;
} Settings;

typedef enum {
    SettingsParseOk, /* current version, all fields valid */
    SettingsParseMigrated, /* older version, upgraded; missing fields defaulted */
    SettingsParseFuture, /* newer version than we know: do not overwrite */
    SettingsParseCorrupt, /* unparseable or out-of-range: do not overwrite */
} SettingsParseResult;

void settings_set_defaults(Settings* s);

/* Parses `len` bytes (not NUL-terminated). `out` is only written on Ok/Migrated;
 * on other results it is set to defaults. Unknown keys are ignored, duplicate or
 * missing required keys and out-of-range values make the file Corrupt. */
SettingsParseResult settings_parse(const char* buf, size_t len, Settings* out);

/* Returns bytes written (excluding NUL), or 0 if `cap` is too small. */
size_t settings_format(const Settings* s, char* buf, size_t cap);

/* Value ladder used by the settings UI (indices fit in uint8_t).
 * 0..60 step 1, 65..300 step 5, 310..600 step 10. */
#define LADDER_COUNT 139
#define LADDER_WORK_FIRST 5 /* ladder index whose value is WORK_MIN */
uint16_t ladder_value(uint8_t index);
uint8_t ladder_nearest(uint16_t value, uint8_t first_index);

typedef enum {
    PhaseWork,
    PhaseRest,
    PhaseDone,
} Phase;

typedef struct {
    Phase phase;
    uint16_t round; /* 1-based */
    uint32_t remaining_s; /* in current phase, rounded up; 0 when done */
} Progress;

/* Stateless: where is a session `elapsed_ms` after start? No rest after the last round. */
void timer_progress(const Settings* s, uint32_t elapsed_ms, Progress* p);
