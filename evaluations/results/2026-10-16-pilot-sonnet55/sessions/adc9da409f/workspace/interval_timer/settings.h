#pragma once

#include <stdint.h>
#include <stdbool.h>

#define SETTINGS_FORMAT_VERSION 2

#define WORK_MIN   5
#define WORK_MAX   600
#define REST_MIN   0
#define REST_MAX   600
#define ROUNDS_MIN 1
#define ROUNDS_MAX 99

typedef struct {
    uint32_t work_s;
    uint32_t rest_s;
    uint32_t rounds;
    bool vibrate;
} IntervalSettings;

typedef enum {
    SettingsLoadOk, // current-version file read cleanly
    SettingsLoadNew, // no file yet, defaults used
    SettingsLoadMigrated, // older version read, values kept, new fields defaulted
    SettingsLoadRecovered, // damaged file: bad/missing fields replaced by defaults
    SettingsLoadFuture, // file written by a newer app; read-only for this session
    SettingsLoadStorageError, // SD card unavailable; defaults used, nothing persisted
} SettingsLoadResult;

typedef enum {
    SettingsSaveOk,
    SettingsSaveBlockedFuture, // would destroy data written by a newer version
    SettingsSaveFailed,
} SettingsSaveResult;

void settings_set_defaults(IntervalSettings* s);

/** Never fails hard: always leaves `s` holding valid, in-range values. */
SettingsLoadResult settings_load(IntervalSettings* s);

/** Atomic-ish: writes a temp file, then replaces the real one. */
SettingsSaveResult settings_save(const IntervalSettings* s);
