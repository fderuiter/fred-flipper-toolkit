#include "settings.h"

#include <flipper_format/flipper_format.h>

#define SETTINGS_FILETYPE "Interval Timer Settings"
#define SETTINGS_VERSION  2
#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP_PATH APP_DATA_PATH("settings.tmp")

void settings_set_defaults(Settings* s) {
    s->work = SETTINGS_WORK_DEFAULT;
    s->rest = SETTINGS_REST_DEFAULT;
    s->rounds = SETTINGS_ROUNDS_DEFAULT;
    s->vibrate = SETTINGS_VIBRATE_DEFAULT;
}

// Reads one integer key; keeps *out untouched and returns false if the key is
// missing, malformed or out of range.
static bool read_ranged(FlipperFormat* ff, const char* key, uint32_t min, uint32_t max, uint32_t* out) {
    int32_t v;
    flipper_format_rewind(ff);
    if(!flipper_format_read_int32(ff, key, &v, 1)) return false;
    if(v < (int32_t)min || v > (int32_t)max) return false;
    *out = (uint32_t)v;
    return true;
}

SettingsLoadResult settings_load(Storage* storage, Settings* s) {
    settings_set_defaults(s);

    if(storage_sd_status(storage) != FSE_OK) return SettingsLoadNoStorage;

    // A crash between removing the old file and renaming the temp one leaves only the temp.
    const char* path = SETTINGS_PATH;
    if(!storage_file_exists(storage, path)) {
        if(storage_file_exists(storage, SETTINGS_TMP_PATH)) {
            path = SETTINGS_TMP_PATH;
        } else {
            return SettingsLoadMissing;
        }
    }

    SettingsLoadResult result = SettingsLoadNoStorage;
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();

    do {
        if(!flipper_format_file_open_existing(ff, path)) break;

        uint32_t version = 0;
        if(!flipper_format_read_header(ff, filetype, &version) ||
           !furi_string_equal(filetype, SETTINGS_FILETYPE) || version == 0) {
            result = SettingsLoadRepaired; // not our file / garbage header: defaults
            break;
        }

        bool all_ok = true;
        all_ok &= read_ranged(ff, "Work", SETTINGS_WORK_MIN, SETTINGS_WORK_MAX, &s->work);
        all_ok &= read_ranged(ff, "Rest", SETTINGS_REST_MIN, SETTINGS_REST_MAX, &s->rest);

        if(version >= 2) {
            all_ok &= read_ranged(
                ff, "Rounds", SETTINGS_ROUNDS_MIN, SETTINGS_ROUNDS_MAX, &s->rounds);
            bool vib;
            flipper_format_rewind(ff);
            if(flipper_format_read_bool(ff, "Vibrate", &vib, 1)) {
                s->vibrate = vib;
            } else {
                all_ok = false;
            }
        }

        if(!all_ok) {
            result = SettingsLoadRepaired;
        } else if(version > SETTINGS_VERSION) {
            result = SettingsLoadNewer;
        } else if(version < SETTINGS_VERSION) {
            result = SettingsLoadMigrated;
        } else {
            result = SettingsLoadOk;
        }
    } while(false);

    furi_string_free(filetype);
    flipper_format_free(ff);

    if(result == SettingsLoadMigrated || (result == SettingsLoadOk && path != SETTINGS_PATH)) {
        // Best effort: if this fails the old file stays intact and we migrate again next time.
        settings_save(storage, s);
    }
    return result;
}

bool settings_save(Storage* storage, const Settings* s) {
    bool ok = false;
    if(storage_sd_status(storage) != FSE_OK) return false;

    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    storage_simply_remove(storage, SETTINGS_TMP_PATH);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_always(ff, SETTINGS_TMP_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_VERSION)) break;
        if(!flipper_format_write_uint32(ff, "Work", &s->work, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rest", &s->rest, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rounds", &s->rounds, 1)) break;
        if(!flipper_format_write_bool(ff, "Vibrate", &s->vibrate, 1)) break;
        ok = true;
    } while(false);
    flipper_format_free(ff); // closes (and flushes) the file

    if(ok) {
        // Replace only after the new file is completely written.
        storage_simply_remove(storage, SETTINGS_PATH);
        ok = storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH) == FSE_OK;
    }
    if(!ok) storage_simply_remove(storage, SETTINGS_TMP_PATH);
    return ok;
}
