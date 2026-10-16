#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <storage/storage.h>

#define SETTINGS_FILETYPE "Interval Timer Settings"
#define SETTINGS_VERSION  2

#define WORK_MIN   5
#define WORK_MAX   600
#define WORK_DEF   30
#define REST_MIN   0
#define REST_MAX   600
#define REST_DEF   10
#define ROUNDS_MIN 1
#define ROUNDS_MAX 99
#define ROUNDS_DEF 8

typedef struct {
    uint32_t work;
    uint32_t rest;
    uint32_t rounds;
    bool vibrate;
} TimerSettings;

typedef enum {
    SettingsLoadOk, // file read, all values valid
    SettingsLoadMigrated, // version 1 file read; should be rewritten as version 2
    SettingsLoadDefaults, // no file (first run) -> defaults, nothing wrong
    SettingsLoadRepaired, // file damaged; invalid/missing values replaced by defaults
    SettingsLoadNewer, // file written by a newer app version; do not overwrite it
    SettingsLoadStorageError, // SD card unavailable -> defaults
} SettingsLoadResult;

void settings_set_defaults(TimerSettings* s);

/** Never fails: on any problem `s` holds usable values and the result says why. */
SettingsLoadResult settings_load(Storage* storage, TimerSettings* s);

/** Atomic-ish save (temp file + rename). Returns false on any storage error. */
bool settings_save(Storage* storage, const TimerSettings* s);
