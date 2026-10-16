// Reviewer-side reference solution for FRED task 02. MIT licensed, original.
#include "settings_store.h"
#include <stdio.h>
#include <string.h>

void settings_defaults(Settings* s) {
    s->work_s = 30;
    s->rest_s = 10;
    s->rounds = 8;
    s->vibrate = true;
}

bool settings_valid(const Settings* s) {
    return s->work_s >= 5 && s->work_s <= 600 && s->rest_s <= 600 && s->rounds >= 1 &&
           s->rounds <= 99;
}

static bool parse_u32(const char* v, size_t n, uint32_t* out) {
    if(n == 0 || n > 10) return false;
    uint64_t x = 0;
    for(size_t i = 0; i < n; i++) {
        if(v[i] < '0' || v[i] > '9') return false;
        x = x * 10 + (uint64_t)(v[i] - '0');
    }
    if(x > UINT32_MAX) return false;
    *out = (uint32_t)x;
    return true;
}

enum {
    K_WORK = 1,
    K_REST = 2,
    K_ROUNDS = 4,
    K_VIBRATE = 8
};

SettingsStatus settings_parse(const char* buf, size_t len, Settings* out) {
    Settings s;
    settings_defaults(&s);
    if(len > SETTINGS_MAX_BYTES) goto corrupt;
    uint32_t version = 0, seen = 0, line_no = 0;
    size_t pos = 0;
    while(pos < len) {
        size_t start = pos;
        while(pos < len && buf[pos] != '\n')
            pos++;
        size_t n = pos - start;
        bool terminated = pos < len;
        if(terminated) pos++;
        const char* line = buf + start;
        if(n && line[n - 1] == '\r') n--;
        line_no++;
        if(!terminated && line_no > 2) goto corrupt; /* truncated last record */
        if(n == 0 || line[0] == '#') continue;
        const char* sep = memchr(line, ':', n);
        if(!sep || (size_t)(sep - line) + 1 >= n || sep[1] != ' ') goto corrupt;
        size_t kl = (size_t)(sep - line);
        const char* v = sep + 2;
        size_t vl = n - kl - 2;
#define KEY(k) (kl == sizeof(k) - 1 && memcmp(line, k, kl) == 0)
        if(line_no == 1) {
            if(!KEY("Filetype") || vl != sizeof(SETTINGS_FILETYPE) - 1 ||
               memcmp(v, SETTINGS_FILETYPE, vl) != 0)
                goto corrupt;
            continue;
        }
        if(line_no == 2) {
            if(!KEY("Version") || !parse_u32(v, vl, &version) || version == 0) goto corrupt;
            if(version > SETTINGS_VERSION) {
                settings_defaults(out);
                return SettingsFutureVersion;
            }
            continue;
        }
        uint32_t bit, x = 0;
        if(KEY("Work"))
            bit = K_WORK;
        else if(KEY("Rest"))
            bit = K_REST;
        else if(KEY("Rounds") && version >= 2)
            bit = K_ROUNDS;
        else if(KEY("Vibrate") && version >= 2)
            bit = K_VIBRATE;
        else
            goto corrupt;
#undef KEY
        if(seen & bit) goto corrupt;
        seen |= bit;
        if(bit == K_VIBRATE) {
            if(vl == 4 && memcmp(v, "true", 4) == 0)
                s.vibrate = true;
            else if(vl == 5 && memcmp(v, "false", 5) == 0)
                s.vibrate = false;
            else
                goto corrupt;
            continue;
        }
        if(!parse_u32(v, vl, &x)) goto corrupt;
        if(bit == K_WORK)
            s.work_s = x;
        else if(bit == K_REST)
            s.rest_s = x;
        else
            s.rounds = x;
    }
    if(version == 0) goto corrupt;
    uint32_t need = version == 1 ? (K_WORK | K_REST) : (K_WORK | K_REST | K_ROUNDS | K_VIBRATE);
    if(seen != need || !settings_valid(&s)) goto corrupt;
    *out = s;
    return version < SETTINGS_VERSION ? SettingsMigrated : SettingsLoaded;
corrupt:
    settings_defaults(out);
    return SettingsCorrupt;
}

size_t settings_serialize(const Settings* s, char* buf, size_t cap) {
    int n = snprintf(
        buf,
        cap,
        "Filetype: %s\nVersion: %u\nWork: %lu\nRest: %lu\nRounds: %lu\nVibrate: %s\n",
        SETTINGS_FILETYPE,
        SETTINGS_VERSION,
        (unsigned long)s->work_s,
        (unsigned long)s->rest_s,
        (unsigned long)s->rounds,
        s->vibrate ? "true" : "false");
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

static SettingsStatus load_one(const SettingsFs* fs, const char* path, Settings* out) {
    char buf[SETTINGS_MAX_BYTES + 1];
    size_t len = 0;
    if(!fs->exists(fs->ctx, path)) {
        settings_defaults(out);
        return SettingsMissing;
    }
    if(!fs->read_all(fs->ctx, path, buf, sizeof(buf), &len)) {
        settings_defaults(out);
        return SettingsCorrupt;
    }
    return settings_parse(buf, len, out);
}

static bool usable(SettingsStatus st) {
    return st == SettingsLoaded || st == SettingsMigrated;
}

SettingsStatus settings_load(const SettingsFs* fs, const SettingsPaths* p, Settings* out) {
    SettingsStatus st = load_one(fs, p->path, out);
    if(st == SettingsFutureVersion) return st; /* never touch a newer file */
    if(usable(st)) {
        if(fs->exists(fs->ctx, p->tmp)) fs->remove(fs->ctx, p->tmp); /* stale tmp */
        return st;
    }
    /* Current missing or bad: an interrupted save may have left a verified tmp or a bak. */
    Settings alt;
    if(fs->exists(fs->ctx, p->tmp) && usable(load_one(fs, p->tmp, &alt))) {
        if(st == SettingsMissing && fs->rename(fs->ctx, p->tmp, p->path)) {
            *out = alt;
            return SettingsLoaded;
        }
    }
    if(fs->exists(fs->ctx, p->bak)) {
        SettingsStatus bs = load_one(fs, p->bak, &alt);
        if(usable(bs)) {
            *out = alt;
            return SettingsMigrated; /* recovered from backup; caller should save */
        }
    }
    return st;
}

bool settings_save(const SettingsFs* fs, const SettingsPaths* p, const Settings* s) {
    char buf[SETTINGS_MAX_BYTES];
    char check[SETTINGS_MAX_BYTES + 1];
    size_t n = settings_serialize(s, buf, sizeof(buf)), m = 0;
    if(n == 0 || !settings_valid(s)) return false;
    if(!fs->write_all(fs->ctx, p->tmp, buf, n)) return false;
    if(!fs->read_all(fs->ctx, p->tmp, check, sizeof(check), &m) || m != n ||
       memcmp(check, buf, n) != 0)
        return false;
    if(fs->exists(fs->ctx, p->path)) {
        if(fs->exists(fs->ctx, p->bak) && !fs->remove(fs->ctx, p->bak)) return false;
        if(!fs->rename(fs->ctx, p->path, p->bak)) return false;
    }
    return fs->rename(fs->ctx, p->tmp, p->path);
}
