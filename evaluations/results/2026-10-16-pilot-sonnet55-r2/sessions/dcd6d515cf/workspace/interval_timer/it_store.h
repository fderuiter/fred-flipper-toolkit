#pragma once

/* Load/save policy over an abstract file interface (host-testable, fault-injectable). */

#include "it_settings.h"

typedef enum {
    ItFsOk,
    ItFsNotExist,
    ItFsError,
} ItFsStatus;

typedef struct {
    /* Reads whole file. ItFsError if unreadable, short read, or larger than cap. */
    ItFsStatus (*read_all)(void* ctx, const char* path, char* buf, size_t cap, size_t* len);
    /* Create/truncate, write all, sync, close. false on any short write or failure. */
    bool (*write_all)(void* ctx, const char* path, const char* data, size_t len);
    /* Target must not exist (the store never relies on overwrite-on-rename). */
    bool (*rename)(void* ctx, const char* from, const char* to);
    ItFsStatus (*remove)(void* ctx, const char* path); /* NotExist is not a failure */
    ItFsStatus (*exists)(void* ctx, const char* path); /* Ok / NotExist / Error */
} ItFsOps;

typedef struct {
    const char* cur;
    const char* tmp;
    const char* bak;
} ItPaths;

typedef enum {
    ItLoadOk,
    ItLoadMigrated, /* v1 values kept; file is rewritten as v2 only when the user saves */
    ItLoadNoFile, /* first run: defaults, saving allowed */
    ItLoadRecoveredBackup, /* cur missing (interrupted save), .bak used; saving allowed */
    /* Below: defaults are in use and saving is BLOCKED so the existing file is never replaced. */
    ItLoadFuture,
    ItLoadCorrupt,
    ItLoadUnreadable, /* SD missing/unmounted/read error */
} ItLoadResult;

bool it_load_blocks_save(ItLoadResult r);

/* `out` always holds valid settings (defaults unless Ok/Migrated/RecoveredBackup). */
ItLoadResult it_store_load(const ItFsOps* fs, void* ctx, const ItPaths* p, ItSettings* out);

/* tmp -> (cur -> bak) -> cur. On failure the previous cur or bak stays loadable.
 * Not claimed power-loss atomic. */
bool it_store_save(const ItFsOps* fs, void* ctx, const ItPaths* p, const ItSettings* s);
