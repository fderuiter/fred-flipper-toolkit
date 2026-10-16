#include "it_store.h"

bool it_load_blocks_save(ItLoadResult r) {
    return r == ItLoadFuture || r == ItLoadCorrupt || r == ItLoadUnreadable;
}

static ItLoadResult from_parse(ItParseResult pr, ItLoadResult ok_result) {
    switch(pr) {
    case ItParseOk:
        return ok_result;
    case ItParseMigrated:
        return ItLoadMigrated;
    case ItParseFuture:
        return ItLoadFuture;
    default:
        return ItLoadCorrupt;
    }
}

ItLoadResult it_store_load(const ItFsOps* fs, void* ctx, const ItPaths* p, ItSettings* out) {
    it_settings_defaults(out);

    /* Stale temp from an interrupted save is never trusted; best effort removal. */
    fs->remove(ctx, p->tmp);

    char buf[IT_MAX_FILE + 1];
    size_t len = 0;
    bool from_backup = false;

    ItFsStatus st = fs->read_all(ctx, p->cur, buf, IT_MAX_FILE, &len);
    if(st == ItFsNotExist) {
        st = fs->read_all(ctx, p->bak, buf, IT_MAX_FILE, &len);
        if(st == ItFsNotExist) return ItLoadNoFile;
        from_backup = true;
    }
    if(st != ItFsOk) return ItLoadUnreadable;

    ItSettings parsed;
    ItParseResult pr = it_settings_parse(buf, len, &parsed);
    if(pr == ItParseOk || pr == ItParseMigrated) *out = parsed;
    return from_parse(pr, from_backup ? ItLoadRecoveredBackup : ItLoadOk);
}

bool it_store_save(const ItFsOps* fs, void* ctx, const ItPaths* p, const ItSettings* s) {
    char buf[IT_MAX_FILE];
    size_t n = it_settings_format(s, buf, sizeof(buf));
    if(n == 0) return false;

    if(!fs->write_all(ctx, p->tmp, buf, n)) {
        fs->remove(ctx, p->tmp);
        return false;
    }

    ItFsStatus cur = fs->exists(ctx, p->cur);
    if(cur == ItFsError) {
        fs->remove(ctx, p->tmp);
        return false;
    }

    bool moved_cur = false;
    if(cur == ItFsOk) {
        if(fs->remove(ctx, p->bak) == ItFsError || !fs->rename(ctx, p->cur, p->bak)) {
            fs->remove(ctx, p->tmp);
            return false;
        }
        moved_cur = true;
    }

    if(!fs->rename(ctx, p->tmp, p->cur)) {
        if(moved_cur) fs->rename(ctx, p->bak, p->cur); /* try to put the old file back */
        fs->remove(ctx, p->tmp);
        return false;
    }
    return true;
}
