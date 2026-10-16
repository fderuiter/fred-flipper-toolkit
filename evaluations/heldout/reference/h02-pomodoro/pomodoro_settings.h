// Reviewer-side reference solution for FRED held-out task H02. MIT licensed, original.
// Pomodoro settings: parse v1/v2/v3, migrate to v3, validate, serialize, and a
// temp-file save/recover sequence over injectable file operations. Pure C99.
// No power-loss atomicity is claimed; device/filesystem behavior is untested.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define POMO_FILETYPE  "Pomodoro Settings"
#define POMO_VERSION   3u
#define POMO_MAX_BYTES 512u

typedef struct {
    uint32_t work_min; /* 1..90 */
    uint32_t short_break_min; /* 1..30 */
    uint32_t long_break_min; /* 1..60 */
    uint32_t cycles; /* 1..12 */
    bool sound;
} PomoSettings;

typedef enum {
    PomoLoaded, /* v3, valid */
    PomoMigrated, /* v1/v2 converted; caller should save */
    PomoMissing, /* no file: defaults */
    PomoCorrupt, /* invalid/truncated: defaults; old file kept as .bak on next save */
    PomoFutureVersion, /* newer: defaults in memory, file must not be overwritten */
} PomoStatus;

typedef struct {
    bool (*read_all)(void* ctx, const char* path, char* buf, size_t cap, size_t* len);
    bool (*write_all)(void* ctx, const char* path, const char* buf, size_t len); /* incl. sync+close */
    bool (*rename)(void* ctx, const char* from, const char* to);
    bool (*remove)(void* ctx, const char* path);
    bool (*exists)(void* ctx, const char* path);
    void* ctx;
} PomoFs;

typedef struct {
    const char* path;
    const char* tmp;
    const char* bak;
} PomoPaths;

void pomo_defaults(PomoSettings* s);
bool pomo_valid(const PomoSettings* s);
/* Legacy rule: a v2 file that still uses `Break` (and has no `ShortBreak`) is
 * accepted as an alias, because v2 betas wrote it; both keys present = corrupt. */
PomoStatus pomo_parse(const char* buf, size_t len, PomoSettings* out);
size_t pomo_serialize(const PomoSettings* s, char* buf, size_t cap);
PomoStatus pomo_load(const PomoFs* fs, const PomoPaths* p, PomoSettings* out);
/* Refuses (returns false) when the on-disk file is a future version. */
bool pomo_save(const PomoFs* fs, const PomoPaths* p, const PomoSettings* s);
