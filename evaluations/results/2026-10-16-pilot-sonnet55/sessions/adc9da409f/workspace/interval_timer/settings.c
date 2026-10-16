#include "settings.h"

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define SETTINGS_FILETYPE "Interval Timer Settings"
#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP_PATH APP_DATA_PATH("settings.tmp")

void settings_set_defaults(IntervalSettings* s) {
    s->work_s = 30;
    s->rest_s = 10;
    s->rounds = 8;
    s->vibrate = true;
}

// Reads an integer key; any failure (missing, overflow, out of range) returns false
// and leaves *out untouched, so the caller keeps its default for that field.
static bool read_ranged(FlipperFormat* ff, const char* key, int32_t min, int32_t max, uint32_t* out) {
    int32_t v = 0;
    flipper_format_rewind(ff);
    if(!flipper_format_read_int32(ff, key, &v, 1)) return false;
    if(v < min || v > max) return false;
    *out = (uint32_t)v;
    return true;
}

SettingsLoadResult settings_load(IntervalSettings* s) {
    settings_set_defaults(s);

    SettingsLoadResult result = SettingsLoadOk;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();

    do {
        FileInfo info;
        FS_Error err = storage_common_stat(storage, SETTINGS_PATH, &info);

        if(err == FSE_NOT_EXIST) {
            // A crash between remove and rename in settings_save leaves only the temp file.
            if(storage_common_stat(storage, SETTINGS_TMP_PATH, &info) == FSE_OK) {
                storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH);
                err = storage_common_stat(storage, SETTINGS_PATH, &info);
            }
        }
        if(err == FSE_NOT_EXIST) {
            result = SettingsLoadNew;
            break;
        }
        if(err != FSE_OK || !flipper_format_file_open_existing(ff, SETTINGS_PATH)) {
            result = SettingsLoadStorageError;
            break;
        }

        uint32_t version = 0;
        if(!flipper_format_read_header(ff, filetype, &version) ||
           !furi_string_equal(filetype, SETTINGS_FILETYPE) || version == 0) {
            result = SettingsLoadRecovered; // unreadable header: keep defaults
            break;
        }

        bool ok = true;
        ok &= read_ranged(ff, "Work", WORK_MIN, WORK_MAX, &s->work_s);
        ok &= read_ranged(ff, "Rest", REST_MIN, REST_MAX, &s->rest_s);

        if(version == 1) {
            // v1 has no Rounds/Vibrate: they keep their defaults.
            result = ok ? SettingsLoadMigrated : SettingsLoadRecovered;
        } else {
            ok &= read_ranged(ff, "Rounds", ROUNDS_MIN, ROUNDS_MAX, &s->rounds);
            flipper_format_rewind(ff);
            ok &= flipper_format_read_bool(ff, "Vibrate", &s->vibrate, 1);
            if(version > SETTINGS_FORMAT_VERSION) {
                // Never overwrite a newer file (it may hold fields we don't know), even if damaged.
                result = SettingsLoadFuture;
            } else {
                result = ok ? SettingsLoadOk : SettingsLoadRecovered;
            }
        }
    } while(false);

    furi_string_free(filetype);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return result;
}

SettingsSaveResult settings_save(const IntervalSettings* s) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    SettingsSaveResult result = SettingsSaveFailed;

    do {
        storage_common_mkdir(storage, APP_DATA_PATH("")); // ok if it already exists
        if(!flipper_format_file_open_always(ff, SETTINGS_TMP_PATH)) break;

        uint32_t work = s->work_s, rest = s->rest_s, rounds = s->rounds;
        bool vibrate = s->vibrate;
        bool ok = flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_FORMAT_VERSION);
        ok = ok && flipper_format_write_uint32(ff, "Work", &work, 1);
        ok = ok && flipper_format_write_uint32(ff, "Rest", &rest, 1);
        ok = ok && flipper_format_write_uint32(ff, "Rounds", &rounds, 1);
        ok = ok && flipper_format_write_bool(ff, "Vibrate", &vibrate, 1);
        flipper_format_file_close(ff);
        if(!ok) {
            storage_common_remove(storage, SETTINGS_TMP_PATH);
            break;
        }

        // Rename does not overwrite, so remove the old file first (recovered in load if we die here).
        FS_Error rm = storage_common_remove(storage, SETTINGS_PATH);
        if(rm != FSE_OK && rm != FSE_NOT_EXIST) break;
        if(storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH) != FSE_OK) break;
        result = SettingsSaveOk;
    } while(false);

    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return result;
}
