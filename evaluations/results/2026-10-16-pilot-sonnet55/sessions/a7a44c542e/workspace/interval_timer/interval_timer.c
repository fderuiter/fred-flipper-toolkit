#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>

#include "it_logic.h"

#define TAG "IntervalTimer"

#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP_PATH APP_DATA_PATH("settings.txt.tmp")

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewAbout,
    ViewRunning,
    ViewMessage,
} ViewId;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuIndex;

typedef struct {
    ItTimer timer;
} RunModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    NotificationApp* notifications;
    Submenu* menu;
    VariableItemList* settings_list;
    VariableItem* item_work;
    VariableItem* item_rest;
    VariableItem* item_rounds;
    VariableItem* item_vibrate;
    Widget* about;
    Widget* message;
    View* running;

    ItSettings settings;
    bool save_allowed; /* false after a future-version file: never overwrite it */
    const char* load_note; /* shown once at startup if non-NULL */

    /* Running-screen state; touched only from the dispatcher thread. */
    ViewId current_view;
    bool active;
    ItTimer timer;
    uint32_t last_second_tick;
} App;

/* ---------- storage (blocking; only called from the app/dispatcher thread, never draw/input) ---------- */

typedef enum {
    ReadOk,
    ReadNotFound,
    ReadError,
} ReadResult;

static ReadResult read_small_file(Storage* storage, const char* path, char* buf, size_t cap, size_t* out_len) {
    File* file = storage_file_alloc(storage);
    ReadResult res = ReadError;
    *out_len = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t total = 0;
        res = ReadOk;
        while(total < cap) {
            uint16_t want = (uint16_t)(cap - total > 128 ? 128 : cap - total);
            uint16_t got = storage_file_read(file, buf + total, want);
            if(got == 0) {
                if(storage_file_get_error(file) != FSE_OK) res = ReadError;
                break;
            }
            total += got;
        }
        *out_len = total;
    } else if(storage_file_get_error(file) == FSE_NOT_EXIST) {
        res = ReadNotFound;
    }
    storage_file_close(file);
    storage_file_free(file);
    return res;
}

static void settings_load(App* app) {
    it_settings_defaults(&app->settings);
    app->save_allowed = true;
    app->load_note = NULL;

    char buf[IT_FILE_MAX_SIZE + 1]; /* +1 so oversize files are detected, not truncated */
    size_t len = 0;
    Storage* storage = furi_record_open(RECORD_STORAGE);

    ReadResult r = read_small_file(storage, SETTINGS_PATH, buf, sizeof(buf), &len);
    if(r == ReadNotFound) {
        /* Interrupted save (old file removed, rename not done): recover the temp copy. */
        r = read_small_file(storage, SETTINGS_TMP_PATH, buf, sizeof(buf), &len);
    }
    furi_record_close(RECORD_STORAGE);

    if(r == ReadNotFound) return; /* first run: defaults, no complaint */
    if(r == ReadError) {
        /* SD missing/unreadable. Defaults; saving may still fail later and will be reported then. */
        app->load_note = "Could not read settings.\nUsing defaults.\nCheck the SD card.";
        return;
    }

    switch(it_settings_parse(buf, len, &app->settings)) {
    case ItLoadOk:
        break;
    case ItLoadMigrated:
        /* v1 -> v2: Work/Rest kept, Rounds/Vibrate defaulted. Rewritten as v2 on next save. */
        FURI_LOG_I(TAG, "Migrated settings v1 -> v2");
        break;
    case ItLoadRepaired:
        app->load_note = "Some saved values were\ninvalid and were reset\nto defaults.";
        break;
    case ItLoadFutureVersion:
        app->save_allowed = false;
        app->load_note = "Settings file is from a\nnewer version. Using\ndefaults; changes will\nnot be saved.";
        break;
    case ItLoadBadFile:
        app->load_note = "Settings file unreadable.\nUsing defaults.";
        break;
    }
}

/* Write temp, check every length, sync, close, then replace. Remove+rename is not atomic;
 * settings_load() recovers the temp file if the main file is missing. */
static bool settings_save(const App* app) {
    if(!app->save_allowed) return false;

    char buf[160];
    size_t len = it_settings_format(&app->settings, buf, sizeof(buf));
    if(len == 0) return false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = false;
    File* file = storage_file_alloc(storage);

    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    if(storage_file_open(file, SETTINGS_TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, buf, len) == len;
        ok = ok && storage_file_sync(file);
    }
    ok = storage_file_close(file) && ok; /* always close */
    storage_file_free(file);

    if(ok) {
        FS_Error e = storage_common_remove(storage, SETTINGS_PATH);
        ok = (e == FSE_OK || e == FSE_NOT_EXIST);
        ok = ok && storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH) == FSE_OK;
    }
    furi_record_close(RECORD_STORAGE);
    if(!ok) FURI_LOG_E(TAG, "Settings save failed");
    return ok;
}

static void switch_view(App* app, ViewId id) {
    app->current_view = id;
    view_dispatcher_switch_to_view(app->dispatcher, id);
}

/* ---------- message screen ---------- */

static void show_message(App* app, const char* text) {
    widget_reset(app->message);
    widget_add_text_scroll_element(app->message, 0, 0, 128, 64, text);
    switch_view(app, ViewMessage);
}

/* ---------- menu ---------- */

static void menu_cb(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        it_timer_start(&app->timer, &app->settings);
        with_view_model(
            app->running, RunModel * m, { m->timer = app->timer; }, true);
        app->last_second_tick = furi_get_tick();
        app->active = true;
        switch_view(app, ViewRunning);
        break;
    case MenuSettings:
        switch_view(app, ViewSettings);
        break;
    case MenuAbout:
        switch_view(app, ViewAbout);
        break;
    }
}

/* ---------- settings screen ---------- */

static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.work = IT_WORK_MIN + variable_item_get_current_value_index(item);
    char t[12];
    snprintf(t, sizeof(t), "%u s", (unsigned)app->settings.work);
    variable_item_set_current_value_text(item, t);
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rest = IT_REST_MIN + variable_item_get_current_value_index(item);
    char t[12];
    snprintf(t, sizeof(t), "%u s", (unsigned)app->settings.rest);
    variable_item_set_current_value_text(item, t);
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rounds = IT_ROUNDS_MIN + variable_item_get_current_value_index(item);
    char t[8];
    snprintf(t, sizeof(t), "%u", (unsigned)app->settings.rounds);
    variable_item_set_current_value_text(item, t);
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.vibrate = variable_item_get_current_value_index(item) != 0;
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
}

/* Push app->settings into the widgets (also used after load). */
static void settings_items_sync(App* app) {
    char t[12];
    variable_item_set_current_value_index(app->item_work, app->settings.work - IT_WORK_MIN);
    snprintf(t, sizeof(t), "%u s", (unsigned)app->settings.work);
    variable_item_set_current_value_text(app->item_work, t);

    variable_item_set_current_value_index(app->item_rest, app->settings.rest - IT_REST_MIN);
    snprintf(t, sizeof(t), "%u s", (unsigned)app->settings.rest);
    variable_item_set_current_value_text(app->item_rest, t);

    variable_item_set_current_value_index(app->item_rounds, app->settings.rounds - IT_ROUNDS_MIN);
    snprintf(t, sizeof(t), "%u", (unsigned)app->settings.rounds);
    variable_item_set_current_value_text(app->item_rounds, t);

    variable_item_set_current_value_index(app->item_vibrate, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(app->item_vibrate, app->settings.vibrate ? "On" : "Off");
}

/* ---------- running screen ---------- */

static void running_draw(Canvas* canvas, void* model) {
    const RunModel* m = model;
    const ItTimer* t = &m->timer;
    char line[32];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(t->phase == ItPhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignBottom, "Back: menu");
        return;
    }
    canvas_draw_str_aligned(
        canvas, 64, 4, AlignCenter, AlignTop, t->phase == ItPhaseWork ? "WORK" : "REST");
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(line, sizeof(line), "%u:%02u", (unsigned)(t->remaining / 60), (unsigned)(t->remaining % 60));
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, line);
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Round %u/%u", (unsigned)t->round, (unsigned)t->cfg.rounds);
    canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignTop, line);
}

/* Back is deliberately not consumed here: it reaches the navigation callback, which cannot be
 * lost to a full queue because it is handled inline by the dispatcher. */
static bool running_input(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return false;
}

/* Dispatcher thread, every 200 ms. Elapsed time comes from furi_get_tick so a late tick catches up. */
static void tick_cb(void* context) {
    App* app = context;
    if(!app->active) return;

    uint32_t now = furi_get_tick();
    bool changed = false;
    while(app->timer.phase != ItPhaseDone && (now - app->last_second_tick) >= 1000) {
        app->last_second_tick += 1000;
        ItEvent ev = it_timer_step(&app->timer);
        changed = true;
        if(ev != ItEventNone && app->timer.cfg.vibrate) {
            notification_message(
                app->notifications,
                ev == ItEventDone ? &sequence_double_vibro : &sequence_single_vibro);
        }
    }
    if(changed) {
        with_view_model(
            app->running, RunModel * m, { m->timer = app->timer; }, true);
    }
}

/* ---------- navigation ---------- */

/* Back pressed and not consumed by the current view. Runs in the dispatcher thread. */
static bool navigation_cb(void* context) {
    App* app = context;
    switch(app->current_view) {
    case ViewRunning:
        app->active = false; /* cancel: stop counting, no further vibration */
        switch_view(app, ViewMenu);
        return true;
    case ViewSettings:
        if(!settings_save(app)) {
            switch_view(app, ViewMessage);
            show_message(
                app,
                app->save_allowed ? "Could not save settings.\nCheck the SD card.\n\nBack: menu" :
                                    "Settings file is from a\nnewer version; changes\nwere not saved.\n\nBack: menu");
        } else {
            switch_view(app, ViewMenu);
        }
        return true;
    case ViewAbout:
    case ViewMessage:
        switch_view(app, ViewMenu);
        return true;
    case ViewMenu:
    default:
        return false; /* stops the dispatcher -> exit */
    }
}

/* ---------- entry ---------- */

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    settings_load(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->dispatcher);
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, navigation_cb);
    view_dispatcher_set_tick_event_callback(app->dispatcher, tick_cb, 200);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_cb, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_cb, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_cb, app);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_list = variable_item_list_alloc();
    app->item_work = variable_item_list_add(
        app->settings_list, "Work", IT_WORK_MAX - IT_WORK_MIN + 1, work_changed, app);
    app->item_rest = variable_item_list_add(
        app->settings_list, "Rest", IT_REST_MAX - IT_REST_MIN + 1, rest_changed, app);
    app->item_rounds = variable_item_list_add(
        app->settings_list, "Rounds", IT_ROUNDS_MAX - IT_ROUNDS_MIN + 1, rounds_changed, app);
    app->item_vibrate = variable_item_list_add(app->settings_list, "Vibrate", 2, vibrate_changed, app);
    settings_items_sync(app);
    view_dispatcher_add_view(
        app->dispatcher, ViewSettings, variable_item_list_get_view(app->settings_list));

    app->about = widget_alloc();
    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "Interval Timer\n\nWork/rest rounds with\noptional vibration.\nSettings are stored in\napps_data/interval_timer.");
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->message = widget_alloc();
    view_dispatcher_add_view(app->dispatcher, ViewMessage, widget_get_view(app->message));

    app->running = view_alloc();
    view_allocate_model(app->running, ViewModelTypeLocking, sizeof(RunModel));
    view_set_draw_callback(app->running, running_draw);
    view_set_input_callback(app->running, running_input);
    view_set_context(app->running, app);
    view_dispatcher_add_view(app->dispatcher, ViewRunning, app->running);

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    if(app->load_note) {
        show_message(app, app->load_note);
        app->current_view = ViewMessage;
    } else {
        switch_view(app, ViewMenu);
    }
    view_dispatcher_run(app->dispatcher);

    /* Teardown: dispatcher has stopped, so no callback can run any more. No timers/threads owned. */
    app->active = false;
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewMessage);
    view_dispatcher_remove_view(app->dispatcher, ViewRunning);
    submenu_free(app->menu);
    variable_item_list_free(app->settings_list);
    widget_free(app->about);
    widget_free(app->message);
    view_free_model(app->running);
    view_free(app->running);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
