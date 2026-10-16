#include "settings.h"

#include <flipper_format/flipper_format.h>
#include <furi.h>

#define SETTINGS_DIR  APP_DATA_PATH("")
#define SETTINGS_PATH APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP  APP_DATA_PATH("settings.tmp")

void settings_set_defaults(TimerSettings* s) {
    s->work = WORK_DEF;
    s->rest = REST_DEF;
    s->rounds = ROUNDS_DEF;
    s->vibrate = true;
}

// Reads one uint32 key. Returns false if missing, unparsable or out of range;
// `out` is then left untouched so the caller's default stays in effect.
static bool read_u32(FlipperFormat* ff, const char* key, uint32_t min, uint32_t max, uint32_t* out) {
    uint32_t v;
    flipper_format_rewind(ff);
    if(!flipper_format_read_uint32(ff, key, &v, 1)) return false;
    if(v < min || v > max) return false;
    *out = v;
    return true;
}

static SettingsLoadResult load_from(Storage* storage, const char* path, TimerSettings* s) {
    SettingsLoadResult result = SettingsLoadOk;
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();

    do {
        if(!flipper_format_file_open_existing(ff, path)) {
            result = SettingsLoadDefaults;
            break;
        }
        uint32_t version = 0;
        if(!flipper_format_read_header(ff, filetype, &version) ||
           furi_string_cmp_str(filetype, SETTINGS_FILETYPE) != 0 || version < 1) {
            result = SettingsLoadRepaired;
            break;
        }

        bool ok = read_u32(ff, "Work", WORK_MIN, WORK_MAX, &s->work);
        ok &= read_u32(ff, "Rest", REST_MIN, REST_MAX, &s->rest);

        if(version >= 2) {
            ok &= read_u32(ff, "Rounds", ROUNDS_MIN, ROUNDS_MAX, &s->rounds);
            bool vib;
            flipper_format_rewind(ff);
            if(flipper_format_read_bool(ff, "Vibrate", &vib, 1)) {
                s->vibrate = vib;
            } else {
                ok = false;
            }
        }

        if(version > SETTINGS_VERSION) {
            // Unknown extra keys are ignored; keep the file untouched on disk.
            result = SettingsLoadNewer;
        } else if(!ok) {
            result = SettingsLoadRepaired;
        } else if(version < SETTINGS_VERSION) {
            result = SettingsLoadMigrated;
        }
    } while(false);

    furi_string_free(filetype);
    flipper_format_free(ff); // closes the file
    return result;
}

SettingsLoadResult settings_load(Storage* storage, TimerSettings* s) {
    settings_set_defaults(s);
    if(!storage) return SettingsLoadStorageError;

    SettingsLoadResult r = load_from(storage, SETTINGS_PATH, s);
    if(r == SettingsLoadDefaults) {
        // Main file absent. A previous save may have been interrupted between
        // removing the old file and renaming the new one; recover from it.
        settings_set_defaults(s);
        SettingsLoadResult t = load_from(storage, SETTINGS_TMP, s);
        if(t != SettingsLoadDefaults) r = t == SettingsLoadOk ? SettingsLoadMigrated : t;
        if(r == SettingsLoadDefaults && storage_sd_status(storage) != FSE_OK) {
            r = SettingsLoadStorageError;
        }
    }
    return r;
}

bool settings_save(Storage* storage, const TimerSettings* s) {
    if(!storage) return false;

    bool ok = false;
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    storage_simply_mkdir(storage, SETTINGS_DIR);
    do {
        if(!flipper_format_file_open_always(ff, SETTINGS_TMP)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_VERSION)) break;
        if(!flipper_format_write_uint32(ff, "Work", &s->work, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rest", &s->rest, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rounds", &s->rounds, 1)) break;
        if(!flipper_format_write_bool(ff, "Vibrate", &s->vibrate, 1)) break;
        ok = true;
    } while(false);
    flipper_format_free(ff); // flushes and closes

    if(!ok) {
        storage_common_remove(storage, SETTINGS_TMP);
        return false;
    }
    storage_common_remove(storage, SETTINGS_PATH);
    // If this rename fails the complete temp file is kept; settings_load() recovers from it.
    return storage_common_rename(storage, SETTINGS_TMP, SETTINGS_PATH) == FSE_OK;
}
