#pragma once

#include <furi.h>
#include <storage/storage.h>

#define SETTINGS_WORK_MIN   5
#define SETTINGS_WORK_MAX   600
#define SETTINGS_REST_MIN   0
#define SETTINGS_REST_MAX   600
#define SETTINGS_ROUNDS_MIN 1
#define SETTINGS_ROUNDS_MAX 99

#define SETTINGS_WORK_DEFAULT   30
#define SETTINGS_REST_DEFAULT   10
#define SETTINGS_ROUNDS_DEFAULT 8
#define SETTINGS_VIBRATE_DEFAULT true

typedef struct {
    uint32_t work;
    uint32_t rest;
    uint32_t rounds;
    bool vibrate;
} Settings;

typedef enum {
    SettingsLoadOk, // file read as-is
    SettingsLoadMissing, // no file yet (first run): defaults
    SettingsLoadMigrated, // old version read, values kept, file rewritten as v2
    SettingsLoadRepaired, // some fields invalid/missing: defaults used for those
    SettingsLoadNewer, // file from a newer release: known fields read, file left untouched
    SettingsLoadNoStorage, // SD card absent/unreadable: defaults, nothing persisted
} SettingsLoadResult;

void settings_set_defaults(Settings* s);

/** Never fails hard: always leaves `s` holding valid values. */
SettingsLoadResult settings_load(Storage* storage, Settings* s);

/** Atomic (write temp, then replace). Returns false on any storage error. */
bool settings_save(Storage* storage, const Settings* s);
