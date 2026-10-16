// Reviewer-side reference solution for FRED task 02. MIT licensed, original.
// Settings format, validation, migration and crash-tolerant save/recover. Pure C99;
// file access goes through SettingsFs so the logic can be host-tested with
// injected failures. No power-loss atomicity is claimed: the sequence below only
// guarantees that a readable previous or new copy survives an interruption at
// any single step *as modeled*; device/filesystem behavior is untested.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SETTINGS_FILETYPE  "Interval Timer Settings"
#define SETTINGS_VERSION   2u
#define SETTINGS_MAX_BYTES 512u

typedef struct {
    uint32_t work_s; /* 5..600 */
    uint32_t rest_s; /* 0..600 */
    uint32_t rounds; /* 1..99 */
    bool vibrate;
} Settings;

typedef enum {
    SettingsLoaded, /* current version, valid */
    SettingsMigrated, /* older version, converted; should be saved */
    SettingsMissing, /* no file: defaults */
    SettingsCorrupt, /* unreadable/invalid: defaults; old file kept as .bak on next save */
    SettingsFutureVersion, /* newer version: defaults in memory, file must not be overwritten */
} SettingsStatus;

typedef struct {
    /* Each returns false on any failure. read_all must not write beyond cap. */
    bool (*read_all)(void* ctx, const char* path, char* buf, size_t cap, size_t* len);
    bool (*write_all)(
        void* ctx,
        const char* path,
        const char* buf,
        size_t len); /* incl. sync+close */
    bool (*rename)(void* ctx, const char* from, const char* to);
    bool (*remove)(void* ctx, const char* path);
    bool (*exists)(void* ctx, const char* path);
    void* ctx;
} SettingsFs;

typedef struct {
    const char* path; /* e.g. "/data/settings.txt" */
    const char* tmp; /* path.tmp */
    const char* bak; /* path.bak */
} SettingsPaths;

void settings_defaults(Settings* s);
bool settings_valid(const Settings* s);
SettingsStatus settings_parse(const char* buf, size_t len, Settings* out);
size_t settings_serialize(const Settings* s, char* buf, size_t cap);
/* Recover from an interrupted save, then load. */
SettingsStatus settings_load(const SettingsFs* fs, const SettingsPaths* p, Settings* out);
/* Write tmp, verify by re-reading, rotate current -> bak, tmp -> current. */
bool settings_save(const SettingsFs* fs, const SettingsPaths* p, const Settings* s);
