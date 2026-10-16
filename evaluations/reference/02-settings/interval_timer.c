// Reviewer-side reference solution for FRED task 02. MIT licensed, original.
// ViewDispatcher (no SceneManager needed for three screens): Submenu, VariableItemList,
// a custom running View, a Widget for About. All callbacks below run in the thread
// that calls view_dispatcher_run (this app thread), except View draw callbacks,
// which run in the GUI thread and only read the locked view model.
#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <stdio.h>
#include "settings_store.h"

#define TAG "IntervalTimer"

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewRunning,
    ViewAbout
} ViewId;
typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout
} MenuIndex;

typedef struct {
    uint32_t round;
    uint32_t remaining_s;
    bool resting;
    bool finished;
} RunModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notifications;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    VariableItemList* settings_list;
    View* running;
    Widget* about;
    Settings settings;
    SettingsStatus load_status;
    bool settings_dirty;
    bool running_active;
    uint32_t tick_count;
} App;

/* ---- Storage backend for SettingsFs (blocking; used only from the app thread) ---- */
static bool fs_read_all(void* ctx, const char* path, char* buf, size_t cap, size_t* len) {
    File* f = storage_file_alloc(ctx);
    bool ok = false;
    if(storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(f);
        if(size < cap) {
            size_t got = storage_file_read(f, buf, (size_t)size);
            ok = got == (size_t)size;
            *len = got;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    return ok;
}
static bool fs_write_all(void* ctx, const char* path, const char* buf, size_t len) {
    File* f = storage_file_alloc(ctx);
    bool ok = storage_file_open(f, path, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(f, buf, len) == len && storage_file_sync(f);
    ok = storage_file_close(f) && ok;
    storage_file_free(f);
    return ok;
}
static bool fs_rename(void* ctx, const char* from, const char* to) {
    return storage_common_rename(ctx, from, to) == FSE_OK;
}
static bool fs_remove(void* ctx, const char* path) {
    return storage_common_remove(ctx, path) == FSE_OK;
}
static bool fs_exists(void* ctx, const char* path) {
    return storage_common_exists(ctx, path);
}

static const SettingsPaths paths = {
    APP_DATA_PATH("settings.txt"),
    APP_DATA_PATH("settings.tmp"),
    APP_DATA_PATH("settings.bak")};

static SettingsFs app_fs(App* app) {
    SettingsFs fs = {fs_read_all, fs_write_all, fs_rename, fs_remove, fs_exists, app->storage};
    return fs;
}

/* ---- Settings screen ---- */
static const uint32_t work_values[] = {5, 10, 15, 20, 30, 45, 60, 90, 120, 180, 300, 600};
static const uint32_t rest_values[] = {0, 5, 10, 15, 20, 30, 45, 60, 90, 120, 300, 600};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static uint8_t index_of(const uint32_t* values, size_t n, uint32_t v) {
    for(size_t i = 0; i < n; i++)
        if(values[i] >= v) return (uint8_t)i;
    return (uint8_t)(n - 1);
}
static void set_text_u32(VariableItem* item, uint32_t v, const char* unit) {
    char text[12];
    snprintf(text, sizeof(text), "%lu%s", (unsigned long)v, unit);
    variable_item_set_current_value_text(item, text);
}
static void on_work(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.work_s = work_values[variable_item_get_current_value_index(item)];
    set_text_u32(item, app->settings.work_s, "s");
    app->settings_dirty = true;
}
static void on_rest(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rest_s = rest_values[variable_item_get_current_value_index(item)];
    set_text_u32(item, app->settings.rest_s, "s");
    app->settings_dirty = true;
}
static void on_rounds(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rounds = (uint32_t)variable_item_get_current_value_index(item) + 1;
    set_text_u32(item, app->settings.rounds, "");
    app->settings_dirty = true;
}
static void on_vibrate(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.vibrate = variable_item_get_current_value_index(item) == 1;
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
    app->settings_dirty = true;
}

static void build_settings_list(App* app) {
    VariableItem* it;
    it = variable_item_list_add(app->settings_list, "Work", COUNT(work_values), on_work, app);
    variable_item_set_current_value_index(
        it, index_of(work_values, COUNT(work_values), app->settings.work_s));
    on_work(it);
    it = variable_item_list_add(app->settings_list, "Rest", COUNT(rest_values), on_rest, app);
    variable_item_set_current_value_index(
        it, index_of(rest_values, COUNT(rest_values), app->settings.rest_s));
    on_rest(it);
    it = variable_item_list_add(app->settings_list, "Rounds", 99, on_rounds, app);
    variable_item_set_current_value_index(it, (uint8_t)(app->settings.rounds - 1));
    on_rounds(it);
    it = variable_item_list_add(app->settings_list, "Vibrate", 2, on_vibrate, app);
    variable_item_set_current_value_index(it, app->settings.vibrate ? 1 : 0);
    on_vibrate(it);
    app->settings_dirty = app->load_status == SettingsMigrated; /* persist a migration */
}

static void save_settings_if_needed(App* app) {
    if(!app->settings_dirty) return;
    if(app->load_status == SettingsFutureVersion) {
        /* Policy: a file from a newer app version is never overwritten silently. */
        FURI_LOG_W(TAG, "newer settings file present; changes kept in memory only");
        return;
    }
    SettingsFs fs = app_fs(app);
    storage_common_mkdir(app->storage, STORAGE_APP_DATA_PATH_PREFIX);
    if(settings_save(&fs, &paths, &app->settings)) {
        app->settings_dirty = false;
        app->load_status = SettingsLoaded;
    } else {
        FURI_LOG_E(TAG, "settings save failed (SD missing or full?)");
    }
}

/* ---- Running screen ---- */
static void running_draw(Canvas* canvas, void* model_ptr) {
    RunModel* m = model_ptr;
    char line[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(m->finished) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done");
    } else {
        snprintf(
            line,
            sizeof(line),
            "Round %lu %s",
            (unsigned long)m->round,
            m->resting ? "rest" : "work");
        canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignCenter, line);
        canvas_set_font(canvas, FontBigNumbers);
        snprintf(line, sizeof(line), "%lu", (unsigned long)m->remaining_s);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, line);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignBottom, "Back: stop");
}

static uint32_t to_menu(void* context) {
    UNUSED(context);
    return ViewMenu;
}

static void start_run(App* app) {
    with_view_model(
        app->running,
        RunModel * m,
        {
            m->round = 1;
            m->remaining_s = app->settings.work_s;
            m->resting = false;
            m->finished = false;
        },
        true);
    app->tick_count = 0;
    app->running_active = true;
    view_dispatcher_switch_to_view(app->dispatcher, ViewRunning);
}

/* Dispatcher tick (app thread), every 250 ms; only counts while the running view is active. */
static void on_tick(void* context) {
    App* app = context;
    if(!app->running_active) return;
    if(++app->tick_count % 4 != 0) return;
    bool phase_change = false;
    with_view_model(
        app->running,
        RunModel * m,
        {
            if(!m->finished) {
                if(m->remaining_s > 1) {
                    m->remaining_s--;
                } else if(!m->resting && app->settings.rest_s > 0) {
                    m->resting = true;
                    m->remaining_s = app->settings.rest_s;
                    phase_change = true;
                } else if(m->round < app->settings.rounds) {
                    m->round++;
                    m->resting = false;
                    m->remaining_s = app->settings.work_s;
                    phase_change = true;
                } else {
                    m->finished = true;
                    phase_change = true;
                }
            }
        },
        true);
    if(phase_change && app->settings.vibrate)
        notification_message(app->notifications, &sequence_single_vibro);
}

/* ---- Navigation ---- */
static void on_menu(void* context, uint32_t index) {
    App* app = context;
    save_settings_if_needed(app); /* app thread: blocking storage is acceptable here */
    if(index == MenuStart)
        start_run(app);
    else if(index == MenuSettings)
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
    else
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
}

static uint32_t settings_previous(void* context) {
    UNUSED(context);
    return ViewMenu;
}

static bool on_back(void* context) {
    /* Called when a view's previous callback returns VIEW_NONE (the menu): exit. */
    App* app = context;
    view_dispatcher_stop(app->dispatcher);
    return true;
}

static bool running_input(InputEvent* event, void* context) {
    App* app = context;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        app->running_active = false; /* stop counting before leaving */
        view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
        return true;
    }
    return false;
}

static bool on_custom(void* context, uint32_t event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

int32_t fred_interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    SettingsFs fs = app_fs(app);
    app->load_status = settings_load(&fs, &paths, &app->settings);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, on_back);
    view_dispatcher_set_custom_event_callback(app->dispatcher, on_custom);
    view_dispatcher_set_tick_event_callback(app->dispatcher, on_tick, furi_ms_to_ticks(250));

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, on_menu, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, on_menu, app);
    submenu_add_item(app->menu, "About", MenuAbout, on_menu, app);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_list = variable_item_list_alloc();
    build_settings_list(app);
    view_set_previous_callback(variable_item_list_get_view(app->settings_list), settings_previous);
    view_dispatcher_add_view(
        app->dispatcher, ViewSettings, variable_item_list_get_view(app->settings_list));

    app->running = view_alloc();
    view_allocate_model(app->running, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->running, app);
    view_set_draw_callback(app->running, running_draw);
    view_set_input_callback(app->running, running_input);
    view_set_previous_callback(app->running, to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewRunning, app->running);

    app->about = widget_alloc();
    widget_add_text_box_element(
        app->about,
        0,
        0,
        128,
        64,
        AlignCenter,
        AlignCenter,
        app->load_status == SettingsFutureVersion ?
            "Settings file is from a\nnewer version; changes\nare not saved." :
            "Interval Timer\nFRED reference app",
        false);
    view_set_previous_callback(widget_get_view(app->about), to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->gui = furi_record_open(RECORD_GUI);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    view_dispatcher_run(app->dispatcher);

    /* Teardown after the dispatcher loop has returned: no tick or input callbacks run now. */
    app->running_active = false;
    save_settings_if_needed(app);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewRunning);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_free(app->dispatcher);
    submenu_free(app->menu);
    variable_item_list_free(app->settings_list);
    view_free(app->running);
    widget_free(app->about);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
