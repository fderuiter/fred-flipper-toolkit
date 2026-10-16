#include "it_settings.h"
#include "it_store.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/dialog_ex.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>

#define TAG "IntervalTimer"

#define SETTINGS_PATH APP_DATA_PATH("settings.fff")
#define SETTINGS_TMP  APP_DATA_PATH("settings.tmp")
#define SETTINGS_BAK  APP_DATA_PATH("settings.bak")

#define TICK_PERIOD_MS 250

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewRun,
    ViewNotice,
} ViewId;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuItem;

typedef enum {
    EventSaveSettings,
} CustomEvent;

typedef enum {
    PhaseWork,
    PhaseRest,
    PhaseDone,
} Phase;

/* ---- view models (guarded by the view's locking model) ---- */

typedef struct {
    ItSettings s;
    uint8_t row;
} SettingsModel;

typedef struct {
    Phase phase;
    uint32_t remaining_s;
    uint32_t round;
    uint32_t rounds;
} RunModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* settings_view;
    View* run_view;
    DialogEx* notice;
    char notice_text[96];

    Storage* storage;
    NotificationApp* notifications;

    /* Persistent settings: touched only in the dispatcher thread. */
    ItSettings settings;
    bool save_blocked;
    bool dirty;

    /* Run state: touched only in the dispatcher thread (tick / previous / event callbacks). */
    bool running;
    bool backlight_forced;
    ItSettings run_cfg;
    Phase phase;
    uint32_t round;
    uint32_t phase_end_tick;
} App;

static const ItPaths settings_paths = {SETTINGS_PATH, SETTINGS_TMP, SETTINGS_BAK};

/* ---- Furi implementation of the file interface (app thread only) ---- */

static ItFsStatus map_err(FS_Error e) {
    if(e == FSE_OK) return ItFsOk;
    if(e == FSE_NOT_EXIST) return ItFsNotExist;
    return ItFsError;
}

static ItFsStatus fs_exists(void* ctx, const char* path) {
    FileInfo info;
    return map_err(storage_common_stat((Storage*)ctx, path, &info));
}

static ItFsStatus fs_read_all(void* ctx, const char* path, char* buf, size_t cap, size_t* len) {
    Storage* storage = ctx;
    FileInfo info;
    ItFsStatus st = map_err(storage_common_stat(storage, path, &info));
    if(st != ItFsOk) return st;
    if(info.size > cap) return ItFsError;

    File* file = storage_file_alloc(storage);
    ItFsStatus result = ItFsError;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        size_t total = 0;
        while(total < info.size) {
            uint16_t got = storage_file_read(file, buf + total, (uint16_t)(info.size - total));
            if(got == 0) break;
            total += got;
        }
        if(total == info.size) {
            *len = total;
            result = ItFsOk;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    return result;
}

static bool fs_write_all(void* ctx, const char* path, const char* data, size_t len) {
    Storage* storage = ctx;
    File* file = storage_file_alloc(storage);
    bool ok = false;
    if(len <= UINT16_MAX && storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        ok = storage_file_write(file, data, (uint16_t)len) == len;
        ok = storage_file_sync(file) && ok;
    }
    ok = storage_file_close(file) && ok;
    storage_file_free(file);
    return ok;
}

static bool fs_rename(void* ctx, const char* from, const char* to) {
    return storage_common_rename((Storage*)ctx, from, to) == FSE_OK;
}

static ItFsStatus fs_remove(void* ctx, const char* path) {
    return map_err(storage_common_remove((Storage*)ctx, path));
}

static const ItFsOps fs_ops = {
    .read_all = fs_read_all,
    .write_all = fs_write_all,
    .rename = fs_rename,
    .remove = fs_remove,
    .exists = fs_exists,
};

/* ---- notice dialog (About / errors) ---- */

static void show_notice(App* app, const char* header, const char* text) {
    snprintf(app->notice_text, sizeof(app->notice_text), "%s", text);
    dialog_ex_set_header(app->notice, header, 64, 2, AlignCenter, AlignTop);
    dialog_ex_set_text(app->notice, app->notice_text, 64, 16, AlignCenter, AlignTop);
    dialog_ex_set_center_button_text(app->notice, "OK");
    view_dispatcher_switch_to_view(app->dispatcher, ViewNotice);
}

static void notice_result_cb(DialogExResult result, void* context) {
    App* app = context;
    if(result == DialogExResultCenter) {
        view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    }
}

static uint32_t notice_previous_cb(void* context) {
    UNUSED(context);
    return ViewMenu;
}

/* ---- settings view ---- */

static void settings_draw_cb(Canvas* canvas, void* model_ptr) {
    const SettingsModel* m = model_ptr;
    char line[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Settings");
    canvas_set_font(canvas, FontSecondary);

    for(uint8_t i = 0; i < 4; i++) {
        switch(i) {
        case 0:
            snprintf(line, sizeof(line), "Work: %lu s", (unsigned long)m->s.work);
            break;
        case 1:
            snprintf(line, sizeof(line), "Rest: %lu s", (unsigned long)m->s.rest);
            break;
        case 2:
            snprintf(line, sizeof(line), "Rounds: %lu", (unsigned long)m->s.rounds);
            break;
        default:
            snprintf(line, sizeof(line), "Vibrate: %s", m->s.vibrate ? "on" : "off");
            break;
        }
        uint8_t y = 24 + i * 11;
        if(i == m->row) canvas_draw_str(canvas, 2, y, ">");
        canvas_draw_str(canvas, 12, y, line);
    }
    canvas_draw_str(canvas, 2, 63, "L/R:+-1  hold:+-10");
}

static uint32_t clamp_add(uint32_t v, int32_t delta, uint32_t lo, uint32_t hi) {
    int64_t r = (int64_t)v + delta;
    if(r < (int64_t)lo) r = lo;
    if(r > (int64_t)hi) r = hi;
    return (uint32_t)r;
}

static bool settings_input_cb(InputEvent* event, void* context) {
    App* app = context;
    bool consumed = false;

    if(event->type != InputTypeShort && event->type != InputTypeLong &&
       event->type != InputTypeRepeat) {
        return false;
    }
    int32_t step = (event->type == InputTypeShort) ? 1 : 10;

    with_view_model(
        app->settings_view,
        SettingsModel * m,
        {
            switch(event->key) {
            case InputKeyUp:
                if(event->type == InputTypeShort) {
                    m->row = (m->row + 3) % 4;
                    consumed = true;
                }
                break;
            case InputKeyDown:
                if(event->type == InputTypeShort) {
                    m->row = (m->row + 1) % 4;
                    consumed = true;
                }
                break;
            case InputKeyLeft:
            case InputKeyRight:
            case InputKeyOk: {
                int32_t d = (event->key == InputKeyLeft) ? -step : step;
                consumed = true;
                if(m->row == 0) {
                    m->s.work = clamp_add(m->s.work, d, IT_WORK_MIN, IT_WORK_MAX);
                } else if(m->row == 1) {
                    m->s.rest = clamp_add(m->s.rest, d, IT_REST_MIN, IT_REST_MAX);
                } else if(m->row == 2) {
                    m->s.rounds = clamp_add(m->s.rounds, d, IT_ROUNDS_MIN, IT_ROUNDS_MAX);
                } else if(event->type == InputTypeShort) {
                    m->s.vibrate = !m->s.vibrate;
                }
                break;
            }
            default:
                break; /* Back is not consumed: dispatcher calls previous callback */
            }
        },
        consumed);
    return consumed;
}

/* Dispatcher thread, inside Back handling: copy the model only, defer storage to an event. */
static uint32_t settings_previous_cb(void* context) {
    App* app = context;
    ItSettings edited;
    with_view_model(app->settings_view, SettingsModel * m, { edited = m->s; }, false);
    if(edited.work != app->settings.work || edited.rest != app->settings.rest ||
       edited.rounds != app->settings.rounds || edited.vibrate != app->settings.vibrate) {
        app->settings = edited;
        app->dirty = true;
        view_dispatcher_send_custom_event(app->dispatcher, EventSaveSettings);
    }
    return ViewMenu;
}

static void save_settings_if_dirty(App* app, bool report) {
    if(!app->dirty) return;
    if(app->save_blocked) {
        app->dirty = false; /* never replace a file we could not understand */
        if(report) {
            show_notice(app, "Not saved", "Existing settings file\nwas kept untouched.");
        }
        return;
    }
    if(it_store_save(&fs_ops, app->storage, &settings_paths, &app->settings)) {
        app->dirty = false;
    } else {
        FURI_LOG_E(TAG, "settings save failed");
        if(report) {
            show_notice(app, "Save failed", "Check the SD card.\nWill retry on exit.");
        }
    }
}

/* ---- running view ---- */

static void run_draw_cb(Canvas* canvas, void* model_ptr) {
    const RunModel* m = model_ptr;
    char buf[24];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(m->phase == PhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignCenter, "Back: menu");
        return;
    }
    snprintf(
        buf,
        sizeof(buf),
        "%s %lu/%lu",
        m->phase == PhaseWork ? "WORK" : "REST",
        (unsigned long)m->round,
        (unsigned long)m->rounds);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(
        buf,
        sizeof(buf),
        "%02lu:%02lu",
        (unsigned long)(m->remaining_s / 60),
        (unsigned long)(m->remaining_s % 60));
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignCenter, "Back: cancel");
}

static void run_publish(App* app, Phase phase, uint32_t remaining_s) {
    uint32_t round = app->round;
    uint32_t rounds = app->run_cfg.rounds;
    with_view_model(
        app->run_view,
        RunModel * m,
        {
            m->phase = phase;
            m->remaining_s = remaining_s;
            m->round = round;
            m->rounds = rounds;
        },
        true);
}

static void run_buzz(App* app, bool final) {
    if(!app->run_cfg.vibrate) return;
    notification_message(
        app->notifications, final ? &sequence_double_vibro : &sequence_single_vibro);
}

static void run_release_backlight(App* app) {
    if(app->backlight_forced) {
        notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
        app->backlight_forced = false;
    }
}

/* Idempotent. Dispatcher thread only. */
static void run_stop(App* app) {
    app->running = false;
    run_release_backlight(app);
}

static void run_start(App* app) {
    app->run_cfg = app->settings;
    app->round = 1;
    app->phase = PhaseWork;
    app->phase_end_tick = furi_get_tick() + app->run_cfg.work * 1000;
    app->running = true;
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    app->backlight_forced = true;
    run_publish(app, PhaseWork, app->run_cfg.work);
    run_buzz(app, false);
    view_dispatcher_switch_to_view(app->dispatcher, ViewRun);
}

static void run_begin_work(App* app, uint32_t now) {
    app->phase = PhaseWork;
    app->phase_end_tick = now + app->run_cfg.work * 1000;
    run_publish(app, PhaseWork, app->run_cfg.work);
    run_buzz(app, false);
}

static void tick_cb(void* context) {
    App* app = context;
    if(!app->running) return;

    uint32_t now = furi_get_tick();
    int32_t left_ms = (int32_t)(app->phase_end_tick - now); /* wrap-safe */
    if(left_ms > 0) {
        run_publish(app, app->phase, ((uint32_t)left_ms + 999) / 1000);
        return;
    }

    if(app->phase == PhaseWork) {
        if(app->round >= app->run_cfg.rounds) {
            run_stop(app);
            app->phase = PhaseDone;
            run_publish(app, PhaseDone, 0);
            run_buzz(app, true);
        } else if(app->run_cfg.rest > 0) {
            app->phase = PhaseRest;
            app->phase_end_tick = now + app->run_cfg.rest * 1000;
            run_publish(app, PhaseRest, app->run_cfg.rest);
            run_buzz(app, false);
        } else {
            app->round++;
            run_begin_work(app, now);
        }
    } else if(app->phase == PhaseRest) {
        app->round++;
        run_begin_work(app, now);
    }
}

/* Back on the running screen: cancel (or leave the Done screen). Never queued, so never dropped. */
static uint32_t run_previous_cb(void* context) {
    App* app = context;
    run_stop(app);
    return ViewMenu;
}

/* ---- menu / dispatcher glue ---- */

static void menu_cb(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        run_start(app);
        break;
    case MenuSettings:
        with_view_model(
            app->settings_view,
            SettingsModel * m,
            {
                m->s = app->settings;
                m->row = 0;
            },
            true);
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    default:
        show_notice(app, "Interval Timer", "Work/rest rounds timer.\nv1.0");
        break;
    }
}

static uint32_t menu_previous_cb(void* context) {
    UNUSED(context);
    return VIEW_NONE; /* exit app */
}

static bool custom_event_cb(void* context, uint32_t event) {
    App* app = context;
    if(event == EventSaveSettings) {
        save_settings_if_dirty(app, true);
        return true;
    }
    return false;
}

static const char* load_notice(ItLoadResult r) {
    switch(r) {
    case ItLoadFuture:
        return "Settings are from a\nnewer version. Using\ndefaults, not saving.";
    case ItLoadCorrupt:
        return "Settings file damaged.\nUsing defaults; file\nis left untouched.";
    case ItLoadUnreadable:
        return "SD card unavailable.\nUsing defaults, not\nsaving.";
    default:
        return NULL;
    }
}

int32_t interval_timer_app(void* p) {
    UNUSED(p);

    /* Record opens block until the service exists; allocations crash on OOM rather than
     * returning NULL, so there is no partial-init failure path to unwind. */
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

    storage_simply_mkdir(app->storage, APP_DATA_PATH(""));
    ItLoadResult loaded = it_store_load(&fs_ops, app->storage, &settings_paths, &app->settings);
    app->save_blocked = it_load_blocks_save(loaded);
    FURI_LOG_I(TAG, "settings load result %d", (int)loaded);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event_cb);
    view_dispatcher_set_tick_event_callback(app->dispatcher, tick_cb, TICK_PERIOD_MS);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_cb, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_cb, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_cb, app);
    view_set_previous_callback(submenu_get_view(app->menu), menu_previous_cb);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_view = view_alloc();
    view_allocate_model(app->settings_view, ViewModelTypeLocking, sizeof(SettingsModel));
    view_set_context(app->settings_view, app);
    view_set_draw_callback(app->settings_view, settings_draw_cb);
    view_set_input_callback(app->settings_view, settings_input_cb);
    view_set_previous_callback(app->settings_view, settings_previous_cb);
    view_dispatcher_add_view(app->dispatcher, ViewSettings, app->settings_view);

    app->run_view = view_alloc();
    view_allocate_model(app->run_view, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->run_view, app);
    view_set_draw_callback(app->run_view, run_draw_cb);
    view_set_previous_callback(app->run_view, run_previous_cb);
    view_dispatcher_add_view(app->dispatcher, ViewRun, app->run_view);

    app->notice = dialog_ex_alloc();
    dialog_ex_set_context(app->notice, app);
    dialog_ex_set_result_callback(app->notice, notice_result_cb);
    view_set_previous_callback(dialog_ex_get_view(app->notice), notice_previous_cb);
    view_dispatcher_add_view(app->dispatcher, ViewNotice, dialog_ex_get_view(app->notice));

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    const char* msg = load_notice(loaded);
    if(msg) {
        show_notice(app, "Settings", msg);
    } else {
        view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    }

    view_dispatcher_run(app->dispatcher);

    /* Teardown: stop producers of effects, final save retry, detach GUI, free, close records. */
    run_stop(app);
    save_settings_if_dirty(app, false);

    view_dispatcher_remove_view(app->dispatcher, ViewNotice);
    view_dispatcher_remove_view(app->dispatcher, ViewRun);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    dialog_ex_free(app->notice);
    view_free(app->run_view);
    view_free(app->settings_view);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
