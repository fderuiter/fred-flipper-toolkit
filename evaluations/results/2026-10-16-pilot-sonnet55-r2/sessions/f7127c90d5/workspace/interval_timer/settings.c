#include "settings.h"

#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define SETTINGS_FILETYPE "Interval Timer Settings"
#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP_PATH APP_DATA_PATH("settings.tmp")

void settings_set_defaults(Settings* s) {
    s->work = 30;
    s->rest = 10;
    s->rounds = 8;
    s->vibrate = true;
}

// Reads one integer key; keeps *out untouched unless present and within [min, max].
// Returns true if the stored value was usable.
static bool read_u32_in_range(FlipperFormat* ff, const char* key, uint32_t min, uint32_t max, uint32_t* out) {
    int32_t v;
    flipper_format_rewind(ff);
    if(!flipper_format_read_int32(ff, key, &v, 1)) return false;
    if(v < (int32_t)min || v > (int32_t)max) return false;
    *out = (uint32_t)v;
    return true;
}

SettingsLoadResult settings_load(Settings* s) {
    settings_set_defaults(s);

    SettingsLoadResult result = SettingsLoadStorageError;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();

    do {
        if(!storage_file_exists(storage, SETTINGS_PATH)) {
            // Distinguish "first run" from "no SD card".
            result = storage_sd_status(storage) == FSE_OK ? SettingsLoadNoFile :
                                                            SettingsLoadStorageError;
            break;
        }
        if(!flipper_format_file_open_existing(ff, SETTINGS_PATH)) {
            result = SettingsLoadStorageError;
            break;
        }

        uint32_t version = 0;
        if(!flipper_format_read_header(ff, filetype, &version) ||
           furi_string_cmp_str(filetype, SETTINGS_FILETYPE) != 0 || version < 1) {
            result = SettingsLoadPartial; // not our file or unreadable header: keep defaults
            break;
        }

        // Field-by-field: a bad field falls back to its default, good ones are kept.
        bool all_ok = true;
        all_ok &= read_u32_in_range(ff, "Work", WORK_MIN, WORK_MAX, &s->work);
        all_ok &= read_u32_in_range(ff, "Rest", REST_MIN, REST_MAX, &s->rest);

        if(version >= 2) {
            // Versions newer than 2 are read best-effort (unknown keys are ignored).
            all_ok &= read_u32_in_range(ff, "Rounds", ROUNDS_MIN, ROUNDS_MAX, &s->rounds);
            flipper_format_rewind(ff);
            bool vib;
            if(flipper_format_read_bool(ff, "Vibrate", &vib, 1)) {
                s->vibrate = vib;
            } else {
                all_ok = false;
            }
        }
        // v1 had no Rounds/Vibrate: defaults are the correct upgrade values.

        if(!all_ok) {
            result = SettingsLoadPartial;
        } else if(version == 1) {
            result = SettingsLoadMigrated;
        } else {
            result = SettingsLoadOk;
        }
    } while(false);

    furi_string_free(filetype);
    flipper_format_free(ff); // closes the file
    furi_record_close(RECORD_STORAGE);

    // Upgrade on disk right away. Failure is harmless: values are still in memory and
    // the old v1 file stays untouched. Damaged/future files are NOT rewritten here so
    // the user's original data survives until they deliberately change settings.
    if(result == SettingsLoadMigrated && !settings_save(s)) {
        result = SettingsLoadOk;
    }
    return result;
}

bool settings_save(const Settings* s) {
    bool ok = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    do {
        storage_simply_mkdir(storage, APP_DATA_PATH(""));
        if(!flipper_format_file_open_always(ff, SETTINGS_TMP_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_VERSION)) break;
        if(!flipper_format_write_uint32(ff, "Work", &s->work, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rest", &s->rest, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rounds", &s->rounds, 1)) break;
        if(!flipper_format_write_bool(ff, "Vibrate", &s->vibrate, 1)) break;
        if(!flipper_format_file_close(ff)) break;

        // Replace the old file only after the new one was fully written.
        storage_common_remove(storage, SETTINGS_PATH);
        if(storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH) != FSE_OK) break;
        ok = true;
    } while(false);

    flipper_format_free(ff);
    if(!ok) storage_common_remove(storage, SETTINGS_TMP_PATH);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
