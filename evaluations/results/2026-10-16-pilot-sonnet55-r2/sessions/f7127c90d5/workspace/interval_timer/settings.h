#pragma once

#include <furi.h>
#include <stdbool.h>
#include <stdint.h>

#define SETTINGS_VERSION 2

#define WORK_MIN   5
#define WORK_MAX   600
#define REST_MIN   0
#define REST_MAX   600
#define ROUNDS_MIN 1
#define ROUNDS_MAX 99

typedef struct {
    uint32_t work;
    uint32_t rest;
    uint32_t rounds;
    bool vibrate;
} Settings;

typedef enum {
    SettingsLoadOk, // file read, every value valid
    SettingsLoadMigrated, // older (v1) file read and rewritten as v2
    SettingsLoadNoFile, // nothing stored yet, defaults used
    SettingsLoadPartial, // file damaged/unsupported: invalid fields replaced by defaults
    SettingsLoadStorageError, // SD card unavailable, defaults used
} SettingsLoadResult;

void settings_set_defaults(Settings* s);

/** Never fails: on any problem `s` holds valid values (defaults for what couldn't be read). */
SettingsLoadResult settings_load(Settings* s);

/** Atomic (temp file + rename). Returns false if the SD card could not be written. */
bool settings_save(const Settings* s);
