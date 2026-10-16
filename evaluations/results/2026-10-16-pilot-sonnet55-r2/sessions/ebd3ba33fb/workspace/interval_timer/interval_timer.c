#include "interval_settings.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define TAG "IntervalTimer"

#define SETTINGS_PATH APP_DATA_PATH("settings.txt")

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewAbout,
    ViewRun,
} ViewId;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuItem;

typedef enum {
    EventSave = 1, /* settings view left with unsaved changes */
} CustomEvent;

typedef struct {
    ItPhase phase;
    uint8_t round;
    uint8_t rounds;
    uint32_t remaining_ms;
} RunModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    VariableItemList* list;
    Widget* about;
    View* run_view;
    NotificationApp* notifications;
    Storage* storage;

    ItStore store;
    ItSettings settings;
    ItLoadStatus load_status;
    bool dirty;
    const char* notice; /* shown as menu header; NULL = none */

    /* Touched only from the dispatcher thread (enter/exit/tick callbacks). */
    ItRun run;
    uint32_t last_tick;
    bool running;
} App;

/* ---- Furi storage backend for ItStore (app thread only, may block) ---- */

static ItIo map_error(FS_Error e) {
    if(e == FSE_OK) return ItIoOk;
    if(e == FSE_NOT_EXIST) return ItIoNotFound;
    return ItIoError;
}

static ItIo fs_read(void* ctx, const char* path, char* buf, size_t cap, size_t* len) {
    Storage* storage = ctx;
    File* file = storage_file_alloc(storage);
    ItIo res = ItIoError;
    *len = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t total = 0;
        while(total < cap) {
            size_t n = storage_file_read(file, buf + total, cap - total);
            if(n == 0) break;
            total += n;
        }
        if(storage_file_get_error(file) == FSE_OK) {
            *len = total;
            res = ItIoOk;
        }
    } else {
        res = map_error(storage_file_get_error(file));
        if(res == ItIoOk) res = ItIoError;
    }
    storage_file_close(file);
    storage_file_free(file);
    return res;
}

static ItIo fs_write(void* ctx, const char* path, const char* data, size_t len) {
    Storage* storage = ctx;
    File* file = storage_file_alloc(storage);
    ItIo res = ItIoError;
    if(storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        if(storage_file_write(file, data, len) == len && storage_file_sync(file)) {
            res = ItIoOk;
        }
    }
    storage_file_close(file); /* void: errors surface via the checks above */
    storage_file_free(file);
    return res;
}

static ItIo fs_rename(void* ctx, const char* from, const char* to) {
    return map_error(storage_common_rename(ctx, from, to));
}

static ItIo fs_remove(void* ctx, const char* path) {
    return map_error(storage_common_remove(ctx, path));
}

static const ItStoreOps fs_ops = {fs_read, fs_write, fs_rename, fs_remove};

/* ---- helpers ---- */

static void app_set_notice(App* app, const char* notice) {
    app->notice = notice;
    submenu_set_header(app->menu, notice ? notice : "Interval Timer");
}

static void app_save_if_dirty(App* app) {
    if(!app->dirty) return;
    if(it_store_save(&app->store, &app->settings)) {
        app->dirty = false;
        if(app->load_status != ItLoadFuture && app->load_status != ItLoadIoError) {
            app_set_notice(app, NULL);
        }
    } else {
        FURI_LOG_E(TAG, "settings save failed");
        app_set_notice(app, app->store.read_only ? "Settings read-only" : "Save failed (SD?)");
        app->dirty = false; /* do not retry in a loop; next change retries */
    }
}

/* ---- menu ---- */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        view_dispatcher_switch_to_view(app->dispatcher, ViewRun);
        break;
    case MenuSettings:
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        break;
    }
}

/* previous callbacks only return an id: no I/O here */
static uint32_t to_menu(void* context) {
    UNUSED(context);
    return ViewMenu;
}

/* ---- settings ---- */

static void format_secs(char* out, size_t cap, uint16_t s) {
    snprintf(out, cap, "%u s", (unsigned)s);
}

static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.work_s = it_secs_from_index(idx, IT_WORK_MIN, IT_WORK_MAX);
    char t[12];
    format_secs(t, sizeof(t), app->settings.work_s);
    variable_item_set_current_value_text(item, t);
    app->dirty = true;
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.rest_s = it_secs_from_index(idx, IT_REST_MIN, IT_REST_MAX);
    char t[12];
    format_secs(t, sizeof(t), app->settings.rest_s);
    variable_item_set_current_value_text(item, t);
    app->dirty = true;
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.rounds = IT_ROUNDS_MIN + idx;
    char t[8];
    snprintf(t, sizeof(t), "%u", (unsigned)app->settings.rounds);
    variable_item_set_current_value_text(item, t);
    app->dirty = true;
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.vibrate = variable_item_get_current_value_index(item) != 0;
    variable_item_set_current_value_text(item, app->settings.vibrate ? "ON" : "OFF");
    app->dirty = true;
}

static void settings_build(App* app) {
    variable_item_list_reset(app->list);
    char t[12];
    VariableItem* item;

    item = variable_item_list_add(
        app->list, "Work", it_secs_count(IT_WORK_MIN, IT_WORK_MAX), work_changed, app);
    variable_item_set_current_value_index(
        item, it_secs_to_index(app->settings.work_s, IT_WORK_MIN, IT_WORK_MAX));
    format_secs(t, sizeof(t), app->settings.work_s);
    variable_item_set_current_value_text(item, t);

    item = variable_item_list_add(
        app->list, "Rest", it_secs_count(IT_REST_MIN, IT_REST_MAX), rest_changed, app);
    variable_item_set_current_value_index(
        item, it_secs_to_index(app->settings.rest_s, IT_REST_MIN, IT_REST_MAX));
    format_secs(t, sizeof(t), app->settings.rest_s);
    variable_item_set_current_value_text(item, t);

    item = variable_item_list_add(
        app->list, "Rounds", IT_ROUNDS_MAX - IT_ROUNDS_MIN + 1, rounds_changed, app);
    variable_item_set_current_value_index(item, app->settings.rounds - IT_ROUNDS_MIN);
    snprintf(t, sizeof(t), "%u", (unsigned)app->settings.rounds);
    variable_item_set_current_value_text(item, t);

    item = variable_item_list_add(app->list, "Vibrate", 2, vibrate_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.vibrate ? "ON" : "OFF");
}

static void settings_enter(void* context) {
    App* app = context;
    settings_build(app); /* rebuild so the list always reflects current values */
}

static void settings_exit(void* context) {
    App* app = context;
    /* runs inside Back handling: defer the slow save to the event handler */
    if(app->dirty) view_dispatcher_send_custom_event(app->dispatcher, EventSave);
}

static bool custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    if(event == EventSave) {
        app_save_if_dirty(app);
        return true;
    }
    return false;
}

/* ---- running screen ---- */

static void run_notify(App* app, ItEvt evt) {
    if(!app->settings.vibrate) return;
    if(evt == ItEvtPhaseChanged) notification_message(app->notifications, &sequence_single_vibro);
    if(evt == ItEvtFinished) notification_message(app->notifications, &sequence_double_vibro);
}

static void run_publish(App* app) {
    with_view_model(
        app->run_view,
        RunModel * m,
        {
            m->phase = app->run.phase;
            m->round = app->run.round;
            m->rounds = app->settings.rounds;
            m->remaining_ms = app->run.remaining_ms;
        },
        true);
}

static void run_enter(void* context) {
    App* app = context;
    it_run_start(&app->run, &app->settings);
    app->last_tick = furi_get_tick();
    app->running = true;
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    run_publish(app);
}

static void run_exit(void* context) {
    App* app = context;
    app->running = false;
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

static void tick_callback(void* context) {
    App* app = context;
    if(!app->running) return;
    uint32_t now = furi_get_tick();
    uint32_t elapsed = now - app->last_tick; /* wrap-safe */
    app->last_tick = now;
    ItEvt evt = it_run_advance(&app->run, &app->settings, elapsed);
    if(evt != ItEvtNone) run_notify(app, evt);
    run_publish(app);
    if(app->run.phase == ItPhaseDone) {
        app->running = false;
        notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
    }
}

static bool run_input(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    /* Back is deliberately not consumed: the dispatcher then switches to to_menu's
     * view, which fires run_exit (the cancel path). */
    return false;
}

static void run_draw(Canvas* canvas, void* _model) {
    RunModel* m = _model;
    char buf[32];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    const char* title = m->phase == ItPhaseWork ? "WORK" :
                        m->phase == ItPhaseRest ? "REST" :
                                                  "DONE";
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, title);

    if(m->phase != ItPhaseDone) {
        uint32_t secs = (m->remaining_ms + 999) / 1000;
        snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, buf);
        canvas_set_font(canvas, FontSecondary);
        snprintf(buf, sizeof(buf), "Round %u/%u", (unsigned)m->round, (unsigned)m->rounds);
        canvas_draw_str_aligned(canvas, 64, 48, AlignCenter, AlignTop, buf);
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Back: cancel");
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "All rounds complete");
        canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Back: menu");
    }
}

/* ---- lifecycle ---- */

static const char* notice_for(ItLoadStatus s) {
    switch(s) {
    case ItLoadFuture:
        return "Newer file: read-only";
    case ItLoadCorrupt:
        return "Settings reset";
    case ItLoadIoError:
        return "SD error: not saved";
    default:
        return NULL;
    }
}

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    /* acquire: records (borrowed) -> storage load -> GUI objects */
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    storage_simply_mkdir(app->storage, APP_DATA_PATH("")); /* best effort */
    if(!it_store_init(&app->store, &fs_ops, app->storage, SETTINGS_PATH)) {
        app->store.read_only = true; /* path too long: never happens with the fixed path */
    }
    app->load_status = it_store_load(&app->store, &app->settings);
    FURI_LOG_I(TAG, "settings load status %d", (int)app->load_status);
    if(app->load_status == ItLoadMigrated) {
        app->dirty = true; /* write v2 once; the v1 file is rotated to .bak, not lost */
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event_callback);
    view_dispatcher_set_tick_event_callback(app->dispatcher, tick_callback, 100);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    app_set_notice(app, notice_for(app->load_status));
    /* no previous callback on the menu: Back there exits the app */
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->list = variable_item_list_alloc();
    View* list_view = variable_item_list_get_view(app->list);
    view_set_context(list_view, app);
    view_set_previous_callback(list_view, to_menu);
    view_set_enter_callback(list_view, settings_enter);
    view_set_exit_callback(list_view, settings_exit);
    view_dispatcher_add_view(app->dispatcher, ViewSettings, list_view);

    app->about = widget_alloc();
    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "\e#Interval Timer\e#\nWork / rest rounds with optional vibration.\n"
        "Settings are stored on the SD card (app data folder).");
    view_set_previous_callback(widget_get_view(app->about), to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->run_view = view_alloc();
    view_allocate_model(app->run_view, ViewModelTypeLockFree, sizeof(RunModel));
    view_set_context(app->run_view, app);
    view_set_draw_callback(app->run_view, run_draw);
    view_set_input_callback(app->run_view, run_input);
    view_set_previous_callback(app->run_view, to_menu);
    view_set_enter_callback(app->run_view, run_enter);
    view_set_exit_callback(app->run_view, run_exit);
    view_dispatcher_add_view(app->dispatcher, ViewRun, app->run_view);

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    if(app->dirty) app_save_if_dirty(app); /* v1 -> v2 migration write */

    view_dispatcher_run(app->dispatcher);

    /* teardown: dispatcher has stopped, so no more callbacks. Save pending
     * changes (e.g. exit directly from Settings), then detach/free views. */
    app_save_if_dirty(app);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);

    view_dispatcher_remove_view(app->dispatcher, ViewRun);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_free_model(app->run_view);
    view_free(app->run_view);
    widget_free(app->about);
    variable_item_list_free(app->list);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
