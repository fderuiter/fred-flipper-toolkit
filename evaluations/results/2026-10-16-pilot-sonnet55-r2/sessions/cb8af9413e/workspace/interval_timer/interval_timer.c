#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "IntervalTimer"

#define SETTINGS_FILETYPE "Interval Timer Settings"
#define SETTINGS_VERSION  2
#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP_PATH APP_DATA_PATH("settings.tmp")

#define WORK_MIN   5
#define WORK_MAX   600
#define REST_MIN   0
#define REST_MAX   600
#define ROUNDS_MIN 1
#define ROUNDS_MAX 99

#define WORK_DEFAULT   30
#define REST_DEFAULT   10
#define ROUNDS_DEFAULT 8

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewRun,
    ViewAbout,
} ViewId;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuIndex;

typedef struct {
    uint32_t work;
    uint32_t rest;
    uint32_t rounds;
    bool vibrate;
} Settings;

typedef enum {
    PhaseWork,
    PhaseRest,
    PhaseDone,
} Phase;

typedef struct {
    Phase phase;
    uint32_t remaining;
    uint32_t round;
    uint32_t rounds;
} RunModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* submenu;
    VariableItemList* var_list;
    Widget* about;
    View* run_view;
    FuriTimer* timer;
    NotificationApp* notifications;
    DialogsApp* dialogs;

    Settings settings;
    // Set when the file on disk was written by a newer app version. We still use
    // the values we understand, but never overwrite the file (it would drop fields).
    bool save_blocked;
    bool dirty;
    ViewId current_view;
} App;

/* ---------------------------------------------------------------- settings */

static void settings_set_defaults(Settings* s) {
    s->work = WORK_DEFAULT;
    s->rest = REST_DEFAULT;
    s->rounds = ROUNDS_DEFAULT;
    s->vibrate = true;
}

// Reads one uint32 key. Missing or out-of-range values leave *out untouched
// (i.e. the default), so one bad field never discards the others.
static void read_ranged(FlipperFormat* ff, const char* key, uint32_t* out, uint32_t lo, uint32_t hi) {
    uint32_t v;
    flipper_format_rewind(ff);
    if(flipper_format_read_uint32(ff, key, &v, 1) && v >= lo && v <= hi) {
        *out = v;
    } else {
        FURI_LOG_W(TAG, "Setting %s missing or invalid, using default", key);
    }
}

static void settings_load(App* app) {
    settings_set_defaults(&app->settings);
    app->save_blocked = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, APP_DATA_PATH(""));

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();
    uint32_t version = 0;

    if(!flipper_format_file_open_existing(ff, SETTINGS_PATH)) {
        FURI_LOG_I(TAG, "No settings file, using defaults");
    } else if(
        !flipper_format_read_header(ff, filetype, &version) ||
        furi_string_cmp_str(filetype, SETTINGS_FILETYPE) != 0 || version < 1) {
        FURI_LOG_W(TAG, "Settings header invalid, using defaults");
    } else {
        // Version 1 has Work/Rest only; Rounds/Vibrate then stay at defaults.
        read_ranged(ff, "Work", &app->settings.work, WORK_MIN, WORK_MAX);
        read_ranged(ff, "Rest", &app->settings.rest, REST_MIN, REST_MAX);
        if(version >= 2) {
            read_ranged(ff, "Rounds", &app->settings.rounds, ROUNDS_MIN, ROUNDS_MAX);
            bool vib;
            flipper_format_rewind(ff);
            if(flipper_format_read_bool(ff, "Vibrate", &vib, 1)) app->settings.vibrate = vib;
        }
        if(version > SETTINGS_VERSION) {
            FURI_LOG_W(TAG, "Settings version %lu is newer, saving disabled", version);
            app->save_blocked = true;
        }
    }

    furi_string_free(filetype);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

// Writes to a temp file and renames over the real one, so a failed or
// interrupted write (SD removed, full) never destroys the previous file.
static bool settings_save(App* app) {
    if(app->save_blocked) return true;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = false;
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    storage_simply_mkdir(storage, APP_DATA_PATH(""));
    do {
        if(!flipper_format_file_open_always(ff, SETTINGS_TMP_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_VERSION)) break;
        if(!flipper_format_write_uint32(ff, "Work", &app->settings.work, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rest", &app->settings.rest, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rounds", &app->settings.rounds, 1)) break;
        if(!flipper_format_write_bool(ff, "Vibrate", &app->settings.vibrate, 1)) break;
        ok = true;
    } while(false);
    // Close (flush) before renaming.
    flipper_format_free(ff);

    if(ok) {
        FS_Error err = storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH);
        if(err != FSE_OK) {
            // Some firmware versions refuse to rename onto an existing file.
            storage_common_remove(storage, SETTINGS_PATH);
            ok = storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH) == FSE_OK;
        }
    }
    if(!ok) {
        storage_common_remove(storage, SETTINGS_TMP_PATH);
        FURI_LOG_E(TAG, "Saving settings failed");
    }

    furi_record_close(RECORD_STORAGE);
    return ok;
}

static void show_save_error(App* app) {
    DialogMessage* msg = dialog_message_alloc();
    dialog_message_set_header(msg, "Save failed", 64, 4, AlignCenter, AlignTop);
    dialog_message_set_text(
        msg, "Settings not saved.\nCheck the SD card.", 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(msg, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, msg);
    dialog_message_free(msg);
}

/* ---------------------------------------------------------------- run view */

static void run_draw(Canvas* canvas, void* _model) {
    RunModel* m = _model;
    char buf[32];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    if(m->phase == PhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Press Back");
        return;
    }

    canvas_draw_str_aligned(
        canvas, 64, 6, AlignCenter, AlignCenter, m->phase == PhaseWork ? "WORK" : "REST");

    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%lu:%02lu", m->remaining / 60, m->remaining % 60);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Round %lu/%lu", m->round, m->rounds);
    canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignCenter, buf);
}

static bool run_input(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    // Back is handled by the previous-view callback; nothing else is bound.
    return false;
}

static void timer_tick(void* context) {
    App* app = context;
    bool buzz = false;
    bool finished = false;

    with_view_model(
        app->run_view,
        RunModel * m,
        {
            if(m->phase != PhaseDone) {
                if(m->remaining > 0) m->remaining--;
                if(m->remaining == 0) {
                    buzz = true;
                    if(m->phase == PhaseWork) {
                        if(m->round >= m->rounds) {
                            m->phase = PhaseDone;
                            finished = true;
                        } else if(app->settings.rest > 0) {
                            m->phase = PhaseRest;
                            m->remaining = app->settings.rest;
                        } else {
                            m->round++;
                            m->remaining = app->settings.work;
                        }
                    } else {
                        m->phase = PhaseWork;
                        m->round++;
                        m->remaining = app->settings.work;
                    }
                }
            }
        },
        true);

    if(buzz && app->settings.vibrate) {
        notification_message(
            app->notifications, finished ? &sequence_double_vibro : &sequence_single_vibro);
    }
    if(finished) {
        furi_timer_stop(app->timer);
        notification_message(app->notifications, &sequence_success);
    }
}

static void run_enter(void* context) {
    App* app = context;
    with_view_model(
        app->run_view,
        RunModel * m,
        {
            m->phase = PhaseWork;
            m->remaining = app->settings.work;
            m->round = 1;
            m->rounds = app->settings.rounds;
        },
        true);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    furi_timer_start(app->timer, furi_ms_to_ticks(1000));
}

static void run_exit(void* context) {
    App* app = context;
    furi_timer_stop(app->timer);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

/* ---------------------------------------------------------- settings view */

// VariableItemList value counts are uint8_t, so 5..600 seconds cannot be one
// step per second. Seconds are stepped 1s up to 60, 5s up to 300, 10s up to 600.
// A stored value between grid points (e.g. 61) is kept and shown exactly until
// the user changes that item.
#define GRID_SEG1_END 60
#define GRID_SEG2_END 300
#define GRID_SEG2_LEN ((GRID_SEG2_END - GRID_SEG1_END) / 5)
#define GRID_SEG3_LEN ((WORK_MAX - GRID_SEG2_END) / 10)

static uint8_t grid_count(uint32_t min) {
    return (GRID_SEG1_END - min + 1) + GRID_SEG2_LEN + GRID_SEG3_LEN;
}

static uint32_t grid_value(uint8_t idx, uint32_t min) {
    uint32_t n1 = GRID_SEG1_END - min + 1;
    if(idx < n1) return min + idx;
    idx -= n1;
    if(idx < GRID_SEG2_LEN) return GRID_SEG1_END + 5 * (idx + 1);
    idx -= GRID_SEG2_LEN;
    return GRID_SEG2_END + 10 * (idx + 1);
}

static uint8_t grid_index(uint32_t v, uint32_t min) {
    uint32_t n1 = GRID_SEG1_END - min + 1;
    if(v <= GRID_SEG1_END) return v - min;
    if(v <= GRID_SEG2_END) {
        uint32_t r = (v - GRID_SEG1_END + 2) / 5;
        return n1 + (r < 1 ? 0 : r - 1);
    }
    uint32_t r = (v - GRID_SEG2_END + 5) / 10;
    return n1 + GRID_SEG2_LEN + (r < 1 ? 0 : r - 1);
}

static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.work = grid_value(i, WORK_MIN);
    app->dirty = true;
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu s", app->settings.work);
    variable_item_set_current_value_text(item, buf);
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.rest = grid_value(i, REST_MIN);
    app->dirty = true;
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu s", app->settings.rest);
    variable_item_set_current_value_text(item, buf);
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t i = variable_item_get_current_value_index(item);
    app->settings.rounds = ROUNDS_MIN + i;
    app->dirty = true;
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", app->settings.rounds);
    variable_item_set_current_value_text(item, buf);
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.vibrate = variable_item_get_current_value_index(item) == 1;
    app->dirty = true;
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
}

// Rebuilds the list from app->settings (called every time the screen is shown,
// so values reflect what was loaded).
static void settings_view_build(App* app) {
    VariableItemList* list = app->var_list;
    variable_item_list_reset(list);
    char buf[12];
    VariableItem* item;

    item = variable_item_list_add(list, "Work", grid_count(WORK_MIN), work_changed, app);
    variable_item_set_current_value_index(item, grid_index(app->settings.work, WORK_MIN));
    snprintf(buf, sizeof(buf), "%lu s", app->settings.work);
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(list, "Rest", grid_count(REST_MIN), rest_changed, app);
    variable_item_set_current_value_index(item, grid_index(app->settings.rest, REST_MIN));
    snprintf(buf, sizeof(buf), "%lu s", app->settings.rest);
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(list, "Rounds", ROUNDS_MAX - ROUNDS_MIN + 1, rounds_changed, app);
    variable_item_set_current_value_index(item, app->settings.rounds - ROUNDS_MIN);
    snprintf(buf, sizeof(buf), "%lu", app->settings.rounds);
    variable_item_set_current_value_text(item, buf);

    item = variable_item_list_add(list, "Vibrate", 2, vibrate_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
}

/* -------------------------------------------------------------- navigation */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewRun);
        break;
    case MenuSettings:
        settings_view_build(app);
        app->dirty = false;
        app->current_view = ViewSettings;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewAbout);
        break;
    }
}

static uint32_t nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t nav_to_menu(void* context) {
    UNUSED(context);
    return ViewMenu;
}

// Back handler for the settings screen (which has no previous-view callback,
// because the list module owns its view context).
static bool navigation_callback(void* context) {
    App* app = context;
    if(app->current_view == ViewSettings) {
        if(app->dirty) {
            if(!settings_save(app)) show_save_error(app);
            app->dirty = false;
        }
        app->current_view = ViewMenu;
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------- main */

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    // Never fails visibly: falls back to defaults on any storage problem.
    settings_load(app);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    submenu_add_item(app->submenu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->submenu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->submenu, "About", MenuAbout, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), nav_exit);
    view_dispatcher_add_view(app->view_dispatcher, ViewMenu, submenu_get_view(app->submenu));

    app->var_list = variable_item_list_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, navigation_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, ViewSettings, variable_item_list_get_view(app->var_list));

    app->run_view = view_alloc();
    view_allocate_model(app->run_view, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->run_view, app);
    view_set_draw_callback(app->run_view, run_draw);
    view_set_input_callback(app->run_view, run_input);
    view_set_enter_callback(app->run_view, run_enter);
    view_set_exit_callback(app->run_view, run_exit);
    view_set_previous_callback(app->run_view, nav_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, ViewRun, app->run_view);
    app->timer = furi_timer_alloc(timer_tick, FuriTimerTypePeriodic, app);

    app->about = widget_alloc();
    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "\e#Interval Timer\e#\n"
        "Work/rest rounds with\n"
        "vibration alerts.\n"
        "Settings are stored in\n"
        "/ext/apps_data/interval_timer/");
    view_set_previous_callback(widget_get_view(app->about), nav_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, ViewAbout, widget_get_view(app->about));

    view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_run(app->view_dispatcher);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    view_dispatcher_remove_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->view_dispatcher, ViewRun);
    view_dispatcher_remove_view(app->view_dispatcher, ViewAbout);
    submenu_free(app->submenu);
    variable_item_list_free(app->var_list);
    view_free(app->run_view);
    widget_free(app->about);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
