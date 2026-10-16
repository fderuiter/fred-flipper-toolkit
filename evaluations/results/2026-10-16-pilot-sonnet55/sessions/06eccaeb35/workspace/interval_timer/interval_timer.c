#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>

#include "settings_logic.h"

#define TAG "IntervalTimer"

#define SETTINGS_PATH APP_DATA_PATH("settings.fff")
#define SETTINGS_TMP  APP_DATA_PATH("settings.fff.tmp")
#define SETTINGS_BAK  APP_DATA_PATH("settings.fff.bak")

#define TICK_PERIOD_MS 250

typedef enum {
    ViewIdMenu,
    ViewIdSettings,
    ViewIdRunning,
    ViewIdAbout,
    ViewIdNotice,
} ViewId;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuItem;

typedef enum {
    PhaseWork,
    PhaseRest,
    PhaseDone,
} Phase;

typedef struct {
    Phase phase;
    uint16_t remaining; /* seconds, rounded up */
    uint8_t round;
    uint8_t rounds;
} RunModel;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    Storage* storage;

    ViewDispatcher* dispatcher;
    Submenu* menu;
    VariableItemList* list;
    View* running;
    Widget* about;
    Popup* notice;

    ITSettings settings;
    bool read_only; /* existing file is newer/oversized: never overwrite it */
    bool dirty; /* settings edited since last successful save */

    /* Run state: touched only from the dispatcher thread (enter/exit/tick). */
    bool active;
    Phase phase;
    uint8_t round;
    uint32_t phase_end; /* furi tick at which the current phase ends */
} App;

/* ---------- Storage (dispatcher/app thread only, never draw/input) ---------- */

typedef enum {
    LoadIoOk,
    LoadIoMissing,
    LoadIoError,
} LoadIo;

/* Reads the whole file into buf (max IT_SETTINGS_MAX_BYTES + 1 to detect oversize). */
static LoadIo read_file(Storage* storage, const char* path, char* buf, size_t* out_len) {
    LoadIo res = LoadIoError;
    File* file = storage_file_alloc(storage);
    if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        res = (storage_file_get_error(file) == FSE_NOT_EXIST) ? LoadIoMissing : LoadIoError;
    } else {
        uint64_t size = storage_file_size(file);
        size_t want = (size > IT_SETTINGS_MAX_BYTES) ? IT_SETTINGS_MAX_BYTES + 1 : (size_t)size;
        size_t got = 0;
        while(got < want) {
            uint16_t n = storage_file_read(file, buf + got, want - got);
            if(n == 0) break;
            got += n;
        }
        if(got == want) {
            *out_len = got;
            res = LoadIoOk;
        } /* short read -> error */
    }
    storage_file_close(file);
    storage_file_free(file);
    return res;
}

typedef enum {
    NoticeNone,
    NoticeLoadError,
    NoticeCorrupt,
    NoticeRepaired,
    NoticeReadOnly,
    NoticeBackup,
    NoticeSaveFailed,
    NoticeSaveSkipped,
} NoticeKind;

static NoticeKind load_settings(App* app) {
    it_settings_defaults(&app->settings);
    app->read_only = false;
    app->dirty = false;

    char* buf = malloc(IT_SETTINGS_MAX_BYTES + 1);
    NoticeKind notice = NoticeNone;

    /* A stale temp file is an interrupted save that never replaced the real file. */
    storage_common_remove(app->storage, SETTINGS_TMP);

    size_t len = 0;
    LoadIo io = read_file(app->storage, SETTINGS_PATH, buf, &len);
    if(io == LoadIoMissing) {
        /* Interrupted save between "settings -> .bak" and "tmp -> settings": use .bak. */
        io = read_file(app->storage, SETTINGS_BAK, buf, &len);
        if(io == LoadIoMissing) {
            free(buf);
            return NoticeNone; /* first run: defaults */
        }
    }

    ITLoadStatus st = ITLoadCorrupt;
    if(io == LoadIoOk) st = it_settings_parse(buf, len, &app->settings);

    if(io == LoadIoError || st == ITLoadCorrupt) {
        /* Primary unusable: try the previous copy before giving up. */
        size_t blen = 0;
        ITSettings bs;
        if(read_file(app->storage, SETTINGS_BAK, buf, &blen) == LoadIoOk) {
            ITLoadStatus bst = it_settings_parse(buf, blen, &bs);
            if(bst == ITLoadOk || bst == ITLoadMigratedV1 || bst == ITLoadRepaired) {
                app->settings = bs;
                free(buf);
                return NoticeBackup;
            }
        }
        it_settings_defaults(&app->settings);
        notice = (io == LoadIoError) ? NoticeLoadError : NoticeCorrupt;
    } else if(st == ITLoadRepaired) {
        notice = NoticeRepaired;
    } else if(it_load_status_is_readonly(st)) {
        app->read_only = true;
        notice = NoticeReadOnly;
    }
    free(buf);
    return notice;
}

static bool save_file(App* app, const char* text, size_t len) {
    Storage* st = app->storage;
    bool ok = false;

    storage_simply_mkdir(st, APP_DATA_PATH(""));
    storage_common_remove(st, SETTINGS_TMP);

    File* file = storage_file_alloc(st);
    if(storage_file_open(file, SETTINGS_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, text, len) == len;
        ok = storage_file_sync(file) && ok;
    }
    ok = storage_file_close(file) && ok;
    storage_file_free(file);

    if(!ok) {
        storage_common_remove(st, SETTINGS_TMP);
        return false;
    }

    /* Keep the previous file as .bak, then move the new one into place.
     * Power-loss atomicity of this sequence is not claimed (unresolved);
     * load_settings() recovers from every intermediate state it can observe. */
    bool had_old = storage_file_exists(st, SETTINGS_PATH);
    if(had_old) {
        FS_Error e = storage_common_remove(st, SETTINGS_BAK);
        if(e != FSE_OK && e != FSE_NOT_EXIST) {
            storage_common_remove(st, SETTINGS_TMP);
            return false;
        }
        if(storage_common_rename(st, SETTINGS_PATH, SETTINGS_BAK) != FSE_OK) {
            storage_common_remove(st, SETTINGS_TMP);
            return false;
        }
    }
    if(storage_common_rename(st, SETTINGS_TMP, SETTINGS_PATH) != FSE_OK) {
        if(had_old) storage_common_rename(st, SETTINGS_BAK, SETTINGS_PATH);
        storage_common_remove(st, SETTINGS_TMP);
        return false;
    }
    return true;
}

static NoticeKind save_settings(App* app) {
    if(!app->dirty) return NoticeNone;
    if(app->read_only) return NoticeSaveSkipped;

    char text[IT_SETTINGS_MAX_BYTES];
    size_t len = it_settings_format(&app->settings, text, sizeof(text));
    if(len == 0) return NoticeSaveFailed; /* cannot happen with UI-limited values */

    if(!save_file(app, text, len)) {
        FURI_LOG_E(TAG, "settings save failed");
        return NoticeSaveFailed;
    }
    app->dirty = false;
    return NoticeNone;
}

/* ---------- Notice popup ---------- */

static void show_notice(App* app, NoticeKind kind) {
    /* Popup keeps the pointers: strings must be static. */
    const char* header = "";
    const char* text = "";
    switch(kind) {
    case NoticeLoadError:
        header = "SD card problem";
        text = "Using defaults";
        break;
    case NoticeCorrupt:
        header = "Settings damaged";
        text = "Defaults restored";
        break;
    case NoticeRepaired:
        header = "Settings repaired";
        text = "Bad values reset";
        break;
    case NoticeReadOnly:
        header = "Newer settings file";
        text = "Kept, not modified";
        break;
    case NoticeBackup:
        header = "Settings damaged";
        text = "Backup restored";
        break;
    case NoticeSaveFailed:
        header = "Save failed";
        text = "Check SD card";
        break;
    case NoticeSaveSkipped:
        header = "Not saved";
        text = "Newer file kept";
        break;
    case NoticeNone:
        return;
    }
    popup_set_header(app->notice, header, 64, 10, AlignCenter, AlignTop);
    popup_set_text(app->notice, text, 64, 34, AlignCenter, AlignTop);
    view_dispatcher_switch_to_view(app->dispatcher, ViewIdNotice);
}

static void notice_done_callback(void* context) {
    App* app = context;
    view_dispatcher_switch_to_view(app->dispatcher, ViewIdMenu);
}

static uint32_t notice_previous_callback(void* context) {
    UNUSED(context);
    return ViewIdMenu;
}

/* ---------- Run logic ---------- */

static uint32_t phase_ms(const App* app, Phase p) {
    return 1000UL * (p == PhaseWork ? app->settings.work : app->settings.rest);
}

static void run_vibrate(App* app, bool final) {
    if(!app->settings.vibrate) return;
    notification_message(
        app->notifications, final ? &sequence_double_vibro : &sequence_single_vibro);
}

static void run_publish(App* app, uint16_t remaining) {
    View* v = app->running;
    uint8_t rounds = app->settings.rounds;
    with_view_model(
        v,
        RunModel * m,
        {
            m->phase = app->phase;
            m->remaining = remaining;
            m->round = app->round;
            m->rounds = rounds;
        },
        true);
}

static void run_enter_callback(void* context) {
    App* app = context;
    app->active = true;
    app->round = 1;
    app->phase = PhaseWork;
    app->phase_end = furi_get_tick() + phase_ms(app, PhaseWork);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    run_vibrate(app, false);
    run_publish(app, app->settings.work);
}

static void run_stop(App* app) {
    if(!app->active) return;
    app->active = false;
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

static void run_exit_callback(void* context) {
    run_stop(context);
}

/* Runs on the dispatcher thread; Back never goes through a queue, so it cannot be dropped. */
static void tick_callback(void* context) {
    App* app = context;
    if(!app->active || app->phase == PhaseDone) return;

    int32_t left = (int32_t)(app->phase_end - furi_get_tick());
    if(left <= 0) {
        if(app->phase == PhaseWork) {
            if(app->round >= app->settings.rounds) {
                app->phase = PhaseDone;
                run_vibrate(app, true);
                run_publish(app, 0);
                return;
            }
            if(app->settings.rest > 0) {
                app->phase = PhaseRest;
                app->phase_end += phase_ms(app, PhaseRest);
            } else {
                app->round++;
                app->phase_end += phase_ms(app, PhaseWork);
            }
        } else {
            app->round++;
            app->phase = PhaseWork;
            app->phase_end += phase_ms(app, PhaseWork);
        }
        run_vibrate(app, false);
        left = (int32_t)(app->phase_end - furi_get_tick());
        if(left < 0) left = 0;
    }
    run_publish(app, (uint16_t)((left + 999) / 1000));
}

static void running_draw_callback(Canvas* canvas, void* model) {
    const RunModel* m = model;
    char buf[24];

    canvas_set_font(canvas, FontPrimary);
    if(m->phase == PhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Finished");
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "%u rounds", (unsigned)m->rounds);
        canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, buf);
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Back: menu");
        return;
    }

    canvas_draw_str_aligned(
        canvas, 64, 2, AlignCenter, AlignTop, m->phase == PhaseWork ? "WORK" : "REST");
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%u", (unsigned)m->remaining);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Round %u/%u", (unsigned)m->round, (unsigned)m->rounds);
    canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignCenter, buf);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "Back: cancel");
}

/* Back is not consumed by the view, so the dispatcher uses this to leave. */
static uint32_t running_previous_callback(void* context) {
    UNUSED(context);
    return ViewIdMenu;
}

/* ---------- Settings screen ---------- */

static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    char buf[12];
    app->settings.work = IT_WORK_MIN + idx;
    app->dirty = true;
    snprintf(buf, sizeof(buf), "%u s", (unsigned)app->settings.work);
    variable_item_set_current_value_text(item, buf);
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    char buf[12];
    app->settings.rest = IT_REST_MIN + idx;
    app->dirty = true;
    snprintf(buf, sizeof(buf), "%u s", (unsigned)app->settings.rest);
    variable_item_set_current_value_text(item, buf);
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    char buf[12];
    app->settings.rounds = IT_ROUNDS_MIN + idx;
    app->dirty = true;
    snprintf(buf, sizeof(buf), "%u", (unsigned)app->settings.rounds);
    variable_item_set_current_value_text(item, buf);
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.vibrate = idx != 0;
    app->dirty = true;
    variable_item_set_current_value_text(item, idx ? "On" : "Off");
}

/* Values are in 1-step increments so every value in the requested ranges is reachable. */
static void settings_build(App* app) {
    VariableItem* item;
    char buf[12];
    variable_item_list_reset(app->list);

    item = variable_item_list_add(
        app->list, "Work", IT_WORK_MAX - IT_WORK_MIN + 1, work_changed, app);
    variable_item_set_current_value_index(item, app->settings.work - IT_WORK_MIN);
    snprintf(buf, sizeof(buf), "%u s", (unsigned)app->settings.work);
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(
        app->list, "Rest", IT_REST_MAX - IT_REST_MIN + 1, rest_changed, app);
    variable_item_set_current_value_index(item, app->settings.rest - IT_REST_MIN);
    snprintf(buf, sizeof(buf), "%u s", (unsigned)app->settings.rest);
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(
        app->list, "Rounds", IT_ROUNDS_MAX - IT_ROUNDS_MIN + 1, rounds_changed, app);
    variable_item_set_current_value_index(item, app->settings.rounds - IT_ROUNDS_MIN);
    snprintf(buf, sizeof(buf), "%u", (unsigned)app->settings.rounds);
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(app->list, "Vibrate", 2, vibrate_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
}

/* Back from settings: save (dispatcher thread, blocking is acceptable here). */
static uint32_t settings_previous_callback(void* context) {
    App* app = context;
    NoticeKind n = save_settings(app);
    if(n != NoticeNone) {
        show_notice(app, n); /* switches to the notice view; returning it is consistent */
        return ViewIdNotice;
    }
    return ViewIdMenu;
}

/* ---------- Menu ---------- */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        view_dispatcher_switch_to_view(app->dispatcher, ViewIdRunning);
        break;
    case MenuSettings:
        settings_build(app);
        view_dispatcher_switch_to_view(app->dispatcher, ViewIdSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewIdAbout);
        break;
    }
}

static uint32_t menu_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE; /* leave the app */
}

static uint32_t about_previous_callback(void* context) {
    UNUSED(context);
    return ViewIdMenu;
}

/* ---------- Lifecycle ---------- */

static App* app_alloc(void) {
    /* Furi allocators and record_open do not return NULL (they block or crash),
     * so there is no partial-init path to unwind here. */
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    it_settings_defaults(&app->settings);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->dispatcher, tick_callback, TICK_PERIOD_MS);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), menu_previous_callback);
    view_dispatcher_add_view(app->dispatcher, ViewIdMenu, submenu_get_view(app->menu));

    app->list = variable_item_list_alloc();
    View* lv = variable_item_list_get_view(app->list);
    view_set_context(lv, app);
    view_set_previous_callback(lv, settings_previous_callback);
    view_dispatcher_add_view(app->dispatcher, ViewIdSettings, lv);

    app->running = view_alloc();
    view_allocate_model(app->running, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->running, app);
    view_set_draw_callback(app->running, running_draw_callback);
    view_set_enter_callback(app->running, run_enter_callback);
    view_set_exit_callback(app->running, run_exit_callback);
    view_set_previous_callback(app->running, running_previous_callback);
    view_dispatcher_add_view(app->dispatcher, ViewIdRunning, app->running);

    app->about = widget_alloc();
    widget_add_string_multiline_element(
        app->about,
        64,
        32,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        "Interval Timer\nWork / rest rounds\nSettings kept on SD");
    View* av = widget_get_view(app->about);
    view_set_previous_callback(av, about_previous_callback);
    view_dispatcher_add_view(app->dispatcher, ViewIdAbout, av);

    app->notice = popup_alloc();
    popup_set_context(app->notice, app);
    popup_set_callback(app->notice, notice_done_callback);
    popup_set_timeout(app->notice, 3000);
    popup_enable_timeout(app->notice);
    view_set_previous_callback(popup_get_view(app->notice), notice_previous_callback);
    view_dispatcher_add_view(app->dispatcher, ViewIdNotice, popup_get_view(app->notice));

    return app;
}

static void app_free(App* app) {
    /* Make sure backlight enforcement is released even if we never left the run view. */
    run_stop(app);

    view_dispatcher_remove_view(app->dispatcher, ViewIdNotice);
    view_dispatcher_remove_view(app->dispatcher, ViewIdAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewIdRunning);
    view_dispatcher_remove_view(app->dispatcher, ViewIdSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewIdMenu);
    view_dispatcher_free(app->dispatcher);

    popup_free(app->notice);
    widget_free(app->about);
    view_free_model(app->running);
    view_free(app->running);
    variable_item_list_free(app->list);
    submenu_free(app->menu);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();

    NoticeKind notice = load_settings(app);

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, ViewIdMenu);
    if(notice != NoticeNone) show_notice(app, notice);

    view_dispatcher_run(app->dispatcher);

    app_free(app);
    return 0;
}
