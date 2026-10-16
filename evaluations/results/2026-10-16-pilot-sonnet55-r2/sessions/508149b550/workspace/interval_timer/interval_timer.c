#include "logic.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define TAG "IntervalTimer"

#define DATA_DIR      EXT_PATH("apps_data/interval_timer")
#define SETTINGS_PATH DATA_DIR "/settings.txt"
#define SETTINGS_TMP  DATA_DIR "/settings.tmp"
#define SETTINGS_BAK  DATA_DIR "/settings.bak"
#define SETTINGS_REJ  DATA_DIR "/settings.rej"

#define TICK_MS 200

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewAbout,
    ViewRun,
} AppView;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuItem;

typedef struct {
    Phase phase;
    uint16_t round;
    uint16_t rounds;
    uint32_t remaining_s;
} RunModel;

typedef enum {
    LoadOk,
    LoadMigrated,
    LoadMissing, /* no file: first run */
    LoadFuture, /* newer format: kept untouched until the user edits settings */
    LoadCorrupt, /* unreadable: kept untouched until the user edits settings */
    LoadIoError, /* SD absent / read failure: nothing known about the file */
} LoadStatus;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notifications;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    VariableItemList* list;
    Widget* about;
    View* run;

    Settings settings;
    bool dirty; /* settings changed since last successful save */
    bool preserve_existing; /* an unreadable/future file sits at SETTINGS_PATH */
    bool save_blocked_by_io; /* load failed with I/O error: file state unknown */

    /* touched only from the dispatcher thread */
    bool running;
    uint32_t start_tick;
    Phase last_phase;
    uint16_t last_round;
} App;

/* ---------- storage (app/dispatcher thread only; never from draw/input) ---------- */

typedef enum {
    ReadOk,
    ReadMissing,
    ReadTooBig,
    ReadError,
} ReadResult;

static ReadResult read_small_file(Storage* st, const char* path, char* buf, size_t cap, size_t* len) {
    File* f = storage_file_alloc(st);
    ReadResult res = ReadError;
    if(!storage_file_open(f, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        res = (storage_file_get_error(f) == FSE_NOT_EXIST) ? ReadMissing : ReadError;
    } else {
        /* read cap bytes; buf has cap = max+1 so a longer file is detectable */
        size_t n = storage_file_read(f, buf, cap);
        if(storage_file_get_error(f) != FSE_OK) {
            res = ReadError;
        } else if(n >= cap) {
            res = ReadTooBig;
        } else {
            *len = n;
            res = ReadOk;
        }
    }
    storage_file_close(f);
    storage_file_free(f);
    return res;
}

static LoadStatus load_one(App* app, const char* path) {
    char buf[SETTINGS_MAX_FILE_BYTES + 1];
    size_t len = 0;
    switch(read_small_file(app->storage, path, buf, sizeof(buf), &len)) {
    case ReadMissing:
        return LoadMissing;
    case ReadError:
        return LoadIoError;
    case ReadTooBig:
        return LoadCorrupt;
    case ReadOk:
        break;
    }
    switch(settings_parse(buf, len, &app->settings)) {
    case SettingsParseOk:
        return LoadOk;
    case SettingsParseMigrated:
        return LoadMigrated;
    case SettingsParseFuture:
        return LoadFuture;
    default:
        return LoadCorrupt;
    }
}

static LoadStatus settings_load(App* app) {
    settings_set_defaults(&app->settings);
    /* Stale temp from an interrupted save is never trusted. */
    storage_common_remove(app->storage, SETTINGS_TMP);

    LoadStatus st = load_one(app, SETTINGS_PATH);
    if(st == LoadMissing) {
        /* Interrupted between the two renames in save: previous copy is the truth. */
        LoadStatus b = load_one(app, SETTINGS_BAK);
        if(b == LoadOk || b == LoadMigrated) st = b;
        if(b == LoadMigrated) app->dirty = true; /* write upgraded file on next save */
    }
    if(st == LoadMigrated) app->dirty = true; /* upgrade v1 on disk; old values kept */
    if(st == LoadFuture || st == LoadCorrupt) app->preserve_existing = true;
    if(st == LoadIoError) app->save_blocked_by_io = true;
    return st;
}

static bool write_tmp(App* app) {
    char buf[160];
    size_t n = settings_format(&app->settings, buf, sizeof(buf));
    if(n == 0) return false;
    File* f = storage_file_alloc(app->storage);
    bool ok = false;
    if(storage_file_open(f, SETTINGS_TMP, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(f, buf, n) == n && storage_file_sync(f);
    }
    ok = storage_file_close(f) && ok;
    storage_file_free(f);
    if(!ok) storage_common_remove(app->storage, SETTINGS_TMP);
    return ok;
}

/* Replace sequence keeps a recoverable copy at every step:
 * tmp written+synced -> (reject old unreadable file) -> main->bak -> tmp->main -> drop bak.
 * Not claimed power-loss atomic; load() recovers from bak if main is missing. */
static bool settings_save(App* app) {
    if(app->save_blocked_by_io) return false; /* never overwrite a file we could not read */
    if(!storage_simply_mkdir(app->storage, DATA_DIR)) return false;
    if(!write_tmp(app)) return false;

    if(app->preserve_existing) {
        /* User edited settings: keep the old unreadable/newer file as .rej instead of deleting. */
        storage_common_remove(app->storage, SETTINGS_REJ);
        if(storage_common_rename(app->storage, SETTINGS_PATH, SETTINGS_REJ) != FSE_OK) {
            storage_common_remove(app->storage, SETTINGS_TMP);
            return false;
        }
        app->preserve_existing = false;
    } else if(storage_common_exists(app->storage, SETTINGS_PATH)) {
        storage_common_remove(app->storage, SETTINGS_BAK);
        if(storage_common_rename(app->storage, SETTINGS_PATH, SETTINGS_BAK) != FSE_OK) {
            storage_common_remove(app->storage, SETTINGS_TMP);
            return false;
        }
    }
    if(storage_common_rename(app->storage, SETTINGS_TMP, SETTINGS_PATH) != FSE_OK) {
        storage_common_rename(app->storage, SETTINGS_BAK, SETTINGS_PATH); /* best effort restore */
        return false;
    }
    storage_common_remove(app->storage, SETTINGS_BAK);
    app->dirty = false;
    return true;
}

static void show_message(App* app, const char* header, const char* text) {
    DialogMessage* m = dialog_message_alloc();
    dialog_message_set_header(m, header, 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(m, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(m, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, m);
    dialog_message_free(m);
}

static void save_if_dirty(App* app) {
    if(!app->dirty) return;
    if(!settings_save(app)) {
        FURI_LOG_E(TAG, "settings save failed");
        show_message(app, "Not saved", "Check SD card.\nSettings apply\nuntil you exit.");
    }
}

/* ---------- running screen ---------- */

static void run_draw(Canvas* canvas, void* model) {
    const RunModel* m = model;
    char line[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(m->phase == PhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignBottom, "Back: menu");
        return;
    }
    canvas_draw_str_aligned(
        canvas, 64, 4, AlignCenter, AlignTop, m->phase == PhaseWork ? "WORK" : "REST");
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(line, sizeof(line), "%lu:%02lu", m->remaining_s / 60, m->remaining_s % 60);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, line);
    canvas_set_font(canvas, FontSecondary);
    snprintf(line, sizeof(line), "Round %u/%u", m->round, m->rounds);
    canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignCenter, line);
    canvas_draw_str_aligned(canvas, 126, 62, AlignRight, AlignBottom, "Back: cancel");
}

static void run_stop(App* app) {
    app->running = false;
}

/* Input callbacks run in the dispatcher thread. Back must never be dropped, so it
 * is handled right here (no queue) and only flips state + switches view. */
static bool run_input(InputEvent* event, void* context) {
    App* app = context;
    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        run_stop(app);
        view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
        return true;
    }
    return false;
}

static void run_publish(App* app, const Progress* p) {
    const uint16_t rounds = app->settings.rounds;
    with_view_model(
        app->run,
        RunModel * m,
        {
            m->phase = p->phase;
            m->round = p->round;
            m->rounds = rounds;
            m->remaining_s = p->remaining_s;
        },
        true);
}

static void run_start(App* app) {
    Progress p;
    timer_progress(&app->settings, 0, &p);
    app->start_tick = furi_get_tick();
    app->last_phase = p.phase;
    app->last_round = p.round;
    app->running = true;
    run_publish(app, &p);
    view_dispatcher_switch_to_view(app->dispatcher, ViewRun);
    if(app->settings.vibrate) notification_message(app->notifications, &sequence_single_vibro);
}

static void tick_callback(void* context) {
    App* app = context;
    if(!app->running) return;
    Progress p;
    timer_progress(&app->settings, furi_get_tick() - app->start_tick, &p);
    run_publish(app, &p);

    if(p.phase == PhaseDone) {
        app->running = false;
        if(app->settings.vibrate)
            notification_message(app->notifications, &sequence_double_vibro);
    } else if(p.phase != app->last_phase || p.round != app->last_round) {
        if(app->settings.vibrate)
            notification_message(app->notifications, &sequence_single_vibro);
    }
    app->last_phase = p.phase;
    app->last_round = p.round;
}

/* ---------- menu / settings / about ---------- */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, index);
}

static bool custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    switch(event) {
    case MenuStart:
        save_if_dirty(app); /* blocking I/O is fine here (dispatcher thread) */
        run_start(app);
        return true;
    case MenuSettings:
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        return true;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        return true;
    }
    return false;
}

static uint32_t previous_to_menu(void* context) {
    UNUSED(context);
    return ViewMenu;
}

static uint32_t previous_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void show_value(VariableItem* item, uint16_t value) {
    char t[8];
    snprintf(t, sizeof(t), "%u", value);
    variable_item_set_current_value_text(item, t);
}

static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.work = ladder_value(idx + LADDER_WORK_FIRST);
    app->dirty = true;
    show_value(item, app->settings.work);
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rest = ladder_value(variable_item_get_current_value_index(item));
    app->dirty = true;
    show_value(item, app->settings.rest);
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rounds = ROUNDS_MIN + variable_item_get_current_value_index(item);
    app->dirty = true;
    show_value(item, app->settings.rounds);
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.vibrate = variable_item_get_current_value_index(item) == 1;
    app->dirty = true;
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
}

/* Items show the exact stored value until the user moves them, so an off-grid value
 * from an older file (e.g. Work: 47) survives an upgrade untouched. */
static void build_settings_list(App* app) {
    VariableItem* it;
    it = variable_item_list_add(
        app->list, "Work (s)", LADDER_COUNT - LADDER_WORK_FIRST, work_changed, app);
    variable_item_set_current_value_index(it, ladder_nearest(app->settings.work, LADDER_WORK_FIRST) - LADDER_WORK_FIRST);
    show_value(it, app->settings.work);

    it = variable_item_list_add(app->list, "Rest (s)", LADDER_COUNT, rest_changed, app);
    variable_item_set_current_value_index(it, ladder_nearest(app->settings.rest, 0));
    show_value(it, app->settings.rest);

    it = variable_item_list_add(
        app->list, "Rounds", ROUNDS_MAX - ROUNDS_MIN + 1, rounds_changed, app);
    variable_item_set_current_value_index(it, app->settings.rounds - ROUNDS_MIN);
    show_value(it, app->settings.rounds);

    it = variable_item_list_add(app->list, "Vibrate", 2, vibrate_changed, app);
    variable_item_set_current_value_index(it, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(it, app->settings.vibrate ? "On" : "Off");
}

/* ---------- lifecycle ---------- */

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    /* Records block until available and cannot fail; malloc crashes rather than
     * returning NULL, so there is no partial-init failure path to unwind. */
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    LoadStatus load = settings_load(app);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event_callback);
    view_dispatcher_set_tick_event_callback(app->dispatcher, tick_callback, TICK_MS);

    app->menu = submenu_alloc();
    submenu_set_header(app->menu, "Interval Timer");
    submenu_add_item(app->menu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), previous_exit);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->list = variable_item_list_alloc();
    build_settings_list(app);
    view_set_previous_callback(variable_item_list_get_view(app->list), previous_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewSettings, variable_item_list_get_view(app->list));

    app->about = widget_alloc();
    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "Interval Timer\n"
        "Work/rest rounds with\n"
        "optional vibration.\n"
        "Settings: SD card,\n"
        "apps_data/interval_timer.\n"
        "Back cancels a run.");
    view_set_previous_callback(widget_get_view(app->about), previous_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->run = view_alloc();
    view_allocate_model(app->run, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->run, app);
    view_set_draw_callback(app->run, run_draw);
    view_set_input_callback(app->run, run_input);
    view_set_previous_callback(app->run, previous_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewRun, app->run);

    if(load == LoadFuture) {
        show_message(app, "Newer settings", "Saved file is from a\nnewer version. Using\ndefaults; file kept.");
    } else if(load == LoadCorrupt) {
        show_message(app, "Settings unreadable", "Using defaults.\nOld file is kept as\nsettings.rej on edit.");
    } else if(load == LoadIoError) {
        show_message(app, "SD card problem", "Using defaults.\nSettings won't be\nsaved this session.");
    }

    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    view_dispatcher_run(app->dispatcher); /* returns after Back on the menu */

    /* Teardown: producers first (no timers/threads here; the tick callback stops with the
     * dispatcher), then views, then dispatcher, then final save, then records. */
    run_stop(app);
    view_dispatcher_remove_view(app->dispatcher, ViewRun);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_free(app->run);
    widget_free(app->about);
    variable_item_list_free(app->list);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);

    save_if_dirty(app);

    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
