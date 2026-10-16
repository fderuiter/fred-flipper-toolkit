#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>

#include "interval_logic.h"

#define TAG "IntervalTimer"

#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP_PATH APP_DATA_PATH("settings.txt.tmp")
#define SETTINGS_BAK_PATH APP_DATA_PATH("settings.txt.bak")

#define TICK_PERIOD_MS 100

typedef enum {
    ViewMain,
    ViewSettings,
    ViewAbout,
    ViewRun,
} ViewId;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuItem;

typedef struct {
    ItPhase phase;
    uint8_t round;
    uint8_t rounds;
    uint16_t secs_left;
} RunModel;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    Storage* storage;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    VariableItemList* list;
    Widget* about;
    View* run_view;

    ItSettings settings;
    bool dirty; /* in-memory settings differ from the file (or file needs rewrite as v2) */
    bool save_blocked; /* file has a newer format version: never overwrite it */
    char status[32]; /* main-menu header; empty when everything is fine */

    char work_text[12];
    char rest_text[12];
    char rounds_text[12];

    ItSession session;
    bool running;
    uint32_t last_tick;
    uint16_t shown_secs;
    ItPhase shown_phase;

    char file_buf[IT_FILE_MAX_SIZE + 1];
} App;

/* ---------- storage (always called from the app/dispatcher thread, never draw/input) ---------- */

typedef enum {
    ReadOk,
    ReadMissing,
    ReadError,
    ReadTooBig,
} ReadResult;

static ReadResult read_file(App* app, const char* path, size_t* len) {
    if(!storage_file_exists(app->storage, path)) return ReadMissing;

    ReadResult r = ReadError;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(file);
        if(size > IT_FILE_MAX_SIZE) {
            r = ReadTooBig;
        } else {
            size_t got = 0;
            r = ReadOk;
            while(got < size) {
                uint16_t n = storage_file_read(file, app->file_buf + got, (uint16_t)(size - got));
                if(n == 0) { /* short read / I/O error: do not treat as success */
                    r = ReadError;
                    break;
                }
                got += n;
            }
            *len = got;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    return r;
}

static void set_status(App* app, const char* text) {
    snprintf(app->status, sizeof(app->status), "%s", text);
    submenu_set_header(app->menu, app->status[0] ? app->status : NULL);
}

/* Order: main, then tmp (complete write interrupted before rotation), then bak. */
static void settings_load(App* app) {
    static const char* const paths[] = {SETTINGS_PATH, SETTINGS_TMP_PATH, SETTINGS_BAK_PATH};
    bool saw_bad = false;
    bool loaded = false;

    it_settings_defaults(&app->settings);

    for(size_t i = 0; i < COUNT_OF(paths) && !loaded; i++) {
        size_t len = 0;
        ReadResult rr = read_file(app, paths[i], &len);
        if(rr == ReadMissing) continue;
        if(rr != ReadOk) {
            FURI_LOG_W(TAG, "Cannot read %s (%d)", paths[i], (int)rr);
            saw_bad = true;
            continue;
        }
        ItSettings parsed;
        switch(it_settings_parse(app->file_buf, len, &parsed)) {
        case ItParseOk:
            app->settings = parsed;
            app->dirty = (i != 0); /* recovered from a fallback copy: rewrite main */
            loaded = true;
            if(i != 0) set_status(app, "Restored backup");
            break;
        case ItParseMigrated:
            app->settings = parsed;
            app->dirty = true; /* rewrite as v2 on next save/exit; v1 copy kept as .bak */
            loaded = true;
            break;
        case ItParseFuture:
            app->settings = parsed;
            app->save_blocked = true;
            loaded = true;
            set_status(app, "Newer file: not saved");
            break;
        case ItParseInvalid:
        default:
            FURI_LOG_W(TAG, "Invalid settings in %s", paths[i]);
            saw_bad = true;
            break;
        }
    }

    if(!loaded && saw_bad) set_status(app, "Bad settings: defaults");

    /* A stale tmp is either already consumed above or older than main; drop it. */
    if(!app->save_blocked && storage_file_exists(app->storage, SETTINGS_TMP_PATH)) {
        storage_common_remove(app->storage, SETTINGS_TMP_PATH);
    }
}

static bool write_tmp(App* app, const char* data, size_t len) {
    bool ok = false;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SETTINGS_TMP_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, data, (uint16_t)len) == len && storage_file_sync(file);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

/* Not claimed power-loss atomic: tmp -> (main -> bak) -> main; load() can recover from any step. */
static bool settings_save(App* app) {
    if(app->save_blocked) return false;

    size_t len = it_settings_serialize(&app->settings, app->file_buf, sizeof(app->file_buf));
    if(len == 0) return false;

    storage_simply_mkdir(app->storage, APP_DATA_PATH(""));

    if(!write_tmp(app, app->file_buf, len)) {
        storage_common_remove(app->storage, SETTINGS_TMP_PATH);
        return false;
    }

    bool had_main = storage_file_exists(app->storage, SETTINGS_PATH);
    if(had_main) {
        storage_common_remove(app->storage, SETTINGS_BAK_PATH);
        if(storage_common_rename(app->storage, SETTINGS_PATH, SETTINGS_BAK_PATH) != FSE_OK) {
            return false; /* main untouched; tmp is complete and will be dropped/recovered */
        }
    }
    if(storage_common_rename(app->storage, SETTINGS_TMP_PATH, SETTINGS_PATH) != FSE_OK) {
        if(had_main) storage_common_rename(app->storage, SETTINGS_BAK_PATH, SETTINGS_PATH);
        return false;
    }
    return true;
}

static void settings_save_if_dirty(App* app) {
    if(!app->dirty || app->save_blocked) return;
    if(settings_save(app)) {
        app->dirty = false;
        if(strcmp(app->status, "Restored backup") == 0 || strncmp(app->status, "SD", 2) == 0) {
            set_status(app, "");
        }
    } else {
        FURI_LOG_E(TAG, "Settings save failed");
        set_status(app, "SD error: not saved");
    }
}

/* ---------- running screen ---------- */

static void run_draw_callback(Canvas* canvas, void* _model) {
    RunModel* m = _model;
    char buf[24];

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    if(m->phase == ItPhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
    } else {
        canvas_draw_str_aligned(
            canvas, 64, 8, AlignCenter, AlignCenter, m->phase == ItPhaseWork ? "WORK" : "REST");
        snprintf(buf, sizeof(buf), "%u:%02u", m->secs_left / 60, m->secs_left % 60);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);
    }
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Round %u/%u", m->round, m->rounds);
    canvas_draw_str_aligned(canvas, 64, 51, AlignCenter, AlignCenter, buf);
    canvas_draw_str_aligned(
        canvas,
        64,
        61,
        AlignCenter,
        AlignCenter,
        m->phase == ItPhaseDone ? "Back: menu" : "Back: cancel");
}

/* Back is handled by the navigation callback; consume nothing here. */
static bool run_input_callback(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return false;
}

static void run_publish(App* app) {
    ItPhase phase = app->session.phase;
    uint8_t round = app->session.round;
    uint16_t secs = (uint16_t)it_session_seconds_left(&app->session);
    uint8_t rounds = app->settings.rounds;
    with_view_model(
        app->run_view,
        RunModel * m,
        {
            m->phase = phase;
            m->round = round;
            m->rounds = rounds;
            m->secs_left = secs;
        },
        true);
    app->shown_secs = secs;
    app->shown_phase = phase;
}

static void run_set_active(App* app, bool active) {
    if(active == app->running) return;
    app->running = active;
    notification_message(
        app->notifications,
        active ? &sequence_display_backlight_enforce_on : &sequence_display_backlight_enforce_auto);
}

static void run_start(App* app) {
    it_session_start(&app->session, &app->settings);
    app->last_tick = furi_get_tick();
    run_publish(app);
    run_set_active(app, true);
    if(app->settings.vibrate) notification_message(app->notifications, &sequence_single_vibro);
}

/* Runs in the dispatcher thread; no timer object, so no callback-lifetime hazards. */
static void tick_callback(void* context) {
    App* app = context;
    if(!app->running) return;

    uint32_t now = furi_get_tick();
    uint32_t elapsed = now - app->last_tick; /* wrap-safe */
    app->last_tick = now;

    uint8_t flags = it_session_step(&app->session, &app->settings, elapsed);
    if(flags & IT_STEP_FINISHED) {
        run_set_active(app, false);
        if(app->settings.vibrate) notification_message(app->notifications, &sequence_double_vibro);
    } else if((flags & IT_STEP_PHASE_CHANGED) && app->settings.vibrate) {
        notification_message(app->notifications, &sequence_single_vibro);
    }

    if(app->session.phase != app->shown_phase ||
       (uint16_t)it_session_seconds_left(&app->session) != app->shown_secs || flags) {
        run_publish(app);
    }
}

static uint32_t run_previous_callback(void* context) {
    App* app = context;
    run_set_active(app, false); /* cancel */
    return ViewMain;
}

/* ---------- menu, settings, about ---------- */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        run_start(app);
        view_dispatcher_switch_to_view(app->dispatcher, ViewRun);
        break;
    case MenuSettings:
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        break;
    default:
        break;
    }
}

static uint32_t main_previous_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE; /* exit app */
}

static uint32_t settings_previous_callback(void* context) {
    App* app = context;
    settings_save_if_dirty(app);
    return ViewMain;
}

static uint32_t about_previous_callback(void* context) {
    UNUSED(context);
    return ViewMain;
}

static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.work_s = IT_WORK_MIN + variable_item_get_current_value_index(item);
    snprintf(app->work_text, sizeof(app->work_text), "%u s", app->settings.work_s);
    variable_item_set_current_value_text(item, app->work_text);
    app->dirty = true;
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rest_s = IT_REST_MIN + variable_item_get_current_value_index(item);
    snprintf(app->rest_text, sizeof(app->rest_text), "%u s", app->settings.rest_s);
    variable_item_set_current_value_text(item, app->rest_text);
    app->dirty = true;
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rounds = IT_ROUNDS_MIN + variable_item_get_current_value_index(item);
    snprintf(app->rounds_text, sizeof(app->rounds_text), "%u", app->settings.rounds);
    variable_item_set_current_value_text(item, app->rounds_text);
    app->dirty = true;
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.vibrate = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
    app->dirty = true;
}

static void build_settings_list(App* app) {
    VariableItem* item;

    item = variable_item_list_add(
        app->list, "Work", IT_WORK_MAX - IT_WORK_MIN + 1, work_changed, app);
    variable_item_set_current_value_index(item, app->settings.work_s - IT_WORK_MIN);
    snprintf(app->work_text, sizeof(app->work_text), "%u s", app->settings.work_s);
    variable_item_set_current_value_text(item, app->work_text);

    item = variable_item_list_add(
        app->list, "Rest", IT_REST_MAX - IT_REST_MIN + 1, rest_changed, app);
    variable_item_set_current_value_index(item, app->settings.rest_s - IT_REST_MIN);
    snprintf(app->rest_text, sizeof(app->rest_text), "%u s", app->settings.rest_s);
    variable_item_set_current_value_text(item, app->rest_text);

    item = variable_item_list_add(
        app->list, "Rounds", IT_ROUNDS_MAX - IT_ROUNDS_MIN + 1, rounds_changed, app);
    variable_item_set_current_value_index(item, app->settings.rounds - IT_ROUNDS_MIN);
    snprintf(app->rounds_text, sizeof(app->rounds_text), "%u", app->settings.rounds);
    variable_item_set_current_value_text(item, app->rounds_text);

    item = variable_item_list_add(app->list, "Vibrate", 2, vibrate_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
}

/* ---------- lifecycle ---------- */

int32_t interval_timer_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->dispatcher, tick_callback, TICK_PERIOD_MS);

    app->menu = submenu_alloc();
    app->list = variable_item_list_alloc();
    app->about = widget_alloc();
    app->run_view = view_alloc();

    /* Main menu (header carries load/save status, so create it before loading). */
    submenu_add_item(app->menu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), main_previous_callback);

    settings_load(app);

    build_settings_list(app);
    view_set_context(variable_item_list_get_view(app->list), app);
    view_set_previous_callback(variable_item_list_get_view(app->list), settings_previous_callback);

    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "\e#Interval Timer\e#\n"
        "Work/rest rounds with\n"
        "optional vibration.\n"
        "Settings are saved to\n"
        "the SD card (app data).\n"
        "Back cancels a run.");
    view_set_previous_callback(widget_get_view(app->about), about_previous_callback);

    view_allocate_model(app->run_view, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->run_view, app);
    view_set_draw_callback(app->run_view, run_draw_callback);
    view_set_input_callback(app->run_view, run_input_callback);
    view_set_previous_callback(app->run_view, run_previous_callback);

    view_dispatcher_add_view(app->dispatcher, ViewMain, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, ViewSettings, variable_item_list_get_view(app->list));
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));
    view_dispatcher_add_view(app->dispatcher, ViewRun, app->run_view);

    view_dispatcher_switch_to_view(app->dispatcher, ViewMain);
    view_dispatcher_run(app->dispatcher);

    /* Teardown: stop run (restores backlight), persist, detach GUI, free views, close records. */
    run_set_active(app, false);
    settings_save_if_dirty(app);

    view_dispatcher_remove_view(app->dispatcher, ViewMain);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewRun);
    view_dispatcher_free(app->dispatcher);

    submenu_free(app->menu);
    variable_item_list_free(app->list);
    widget_free(app->about);
    view_free_model(app->run_view);
    view_free(app->run_view);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
