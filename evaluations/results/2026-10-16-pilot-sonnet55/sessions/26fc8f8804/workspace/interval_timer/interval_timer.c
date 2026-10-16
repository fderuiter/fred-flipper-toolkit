#include "settings.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>
#include <stdio.h>

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
} MenuItem;

typedef enum {
    RunEventTick = 1,
} RunEvent;

typedef enum {
    PhaseWork,
    PhaseRest,
    PhaseDone,
} Phase;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notifications;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* settings_view;
    View* run_view;
    Widget* about;
    FuriTimer* tick;

    TimerSettings settings; // only touched from the GUI thread / view callbacks
    bool storage_ok; // false -> persistence unavailable, notice shown
    bool block_save; // file on disk is from a newer version; never overwrite it
    bool dirty;
} App;

typedef struct {
    TimerSettings* settings; // points into App
    uint8_t selected;
    const char* notice;
} SettingsModel;

typedef struct {
    Phase phase;
    uint32_t round;
    uint32_t rounds;
    uint32_t remaining;
    uint32_t rest;
    bool vibrate;
} RunModel;

/* ---------------------------------------------------------------- settings */

static const char* settings_notice(const App* app, SettingsLoadResult r) {
    UNUSED(app);
    switch(r) {
    case SettingsLoadStorageError:
        return "No SD card: not saved";
    case SettingsLoadRepaired:
        return "Bad file: defaults used";
    case SettingsLoadNewer:
        return "Newer file: not saved";
    default:
        return NULL;
    }
}

static void app_save(App* app, SettingsModel* model) {
    if(!app->dirty || app->block_save) return;
    if(settings_save(app->storage, &app->settings)) {
        app->dirty = false;
        if(model) model->notice = NULL;
    } else {
        if(model) model->notice = "Save failed (SD?)";
    }
}

#define SETTINGS_ROWS 4

static void settings_draw(Canvas* canvas, void* _model) {
    SettingsModel* m = _model;
    const TimerSettings* s = m->settings;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Settings");

    const char* labels[SETTINGS_ROWS] = {"Work (s)", "Rest (s)", "Rounds", "Vibrate"};
    char value[12];
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t i = 0; i < SETTINGS_ROWS; i++) {
        uint8_t y = 24 + i * 11;
        switch(i) {
        case 0:
            snprintf(value, sizeof(value), "%lu", s->work);
            break;
        case 1:
            snprintf(value, sizeof(value), "%lu", s->rest);
            break;
        case 2:
            snprintf(value, sizeof(value), "%lu", s->rounds);
            break;
        default:
            snprintf(value, sizeof(value), "%s", s->vibrate ? "On" : "Off");
        }
        if(i == m->selected) {
            canvas_draw_box(canvas, 0, y - 9, 128, 11);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y, labels[i]);
        canvas_draw_str_aligned(canvas, 124, y, AlignRight, AlignBottom, value);
        canvas_set_color(canvas, ColorBlack);
    }
    if(m->notice) {
        canvas_draw_str_aligned(canvas, 126, 10, AlignRight, AlignBottom, m->notice);
    }
}

static void adjust(uint32_t* v, int32_t delta, uint32_t min, uint32_t max) {
    int32_t n = (int32_t)*v + delta;
    if(n < (int32_t)min) n = min;
    if(n > (int32_t)max) n = max;
    *v = n;
}

static bool settings_input(InputEvent* event, void* ctx) {
    App* app = ctx;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        // Held Left/Right repeats in steps of 5 to cross the 5..600 range quickly.
        int32_t step = event->type == InputTypeRepeat ? 5 : 1;
        int32_t dir = 0;
        switch(event->key) {
        case InputKeyUp:
        case InputKeyDown:
            with_view_model(
                app->settings_view,
                SettingsModel * m,
                {
                    m->selected = (m->selected + (event->key == InputKeyDown ? 1 : SETTINGS_ROWS - 1)) %
                                  SETTINGS_ROWS;
                },
                true);
            return true;
        case InputKeyLeft:
            dir = -1;
            break;
        case InputKeyRight:
        case InputKeyOk:
            dir = 1;
            break;
        default:
            return false;
        }
        with_view_model(
            app->settings_view,
            SettingsModel * m,
            {
                TimerSettings* s = &app->settings;
                switch(m->selected) {
                case 0:
                    adjust(&s->work, dir * step, WORK_MIN, WORK_MAX);
                    break;
                case 1:
                    adjust(&s->rest, dir * step, REST_MIN, REST_MAX);
                    break;
                case 2:
                    adjust(&s->rounds, dir * step, ROUNDS_MIN, ROUNDS_MAX);
                    break;
                default:
                    s->vibrate = !s->vibrate;
                }
            },
            true);
        app->dirty = true;
        consumed = true;
    }
    return consumed;
}

static uint32_t settings_back(void* ctx) {
    App* app = ctx;
    // Persist when leaving the screen (not on every key press, to spare the SD card).
    with_view_model(app->settings_view, SettingsModel * m, { app_save(app, m); }, true);
    return ViewMenu;
}

/* ----------------------------------------------------------------- running */

static void run_draw(Canvas* canvas, void* _model) {
    RunModel* m = _model;
    char buf[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(m->phase == PhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Press Back");
        return;
    }

    snprintf(buf, sizeof(buf), "%s  %lu/%lu", m->phase == PhaseWork ? "WORK" : "REST", m->round, m->rounds);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%02lu:%02lu", m->remaining / 60, m->remaining % 60);
    canvas_draw_str_aligned(canvas, 64, 34, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignBottom, "Back: cancel");
}

static void run_notify(App* app, bool vibrate, bool finished) {
    if(finished) {
        notification_message(app->notifications, &sequence_success);
        if(vibrate) notification_message(app->notifications, &sequence_double_vibro);
    } else if(vibrate) {
        notification_message(app->notifications, &sequence_single_vibro);
    }
}

static void run_tick_cb(void* ctx) {
    App* app = ctx;
    view_dispatcher_send_custom_event(app->dispatcher, RunEventTick);
}

static bool run_custom_event(uint32_t event, void* ctx) {
    App* app = ctx;
    if(event != RunEventTick) return false;

    bool notify = false, finished = false, vibrate = false;
    with_view_model(
        app->run_view,
        RunModel * m,
        {
            if(m->phase != PhaseDone) {
                if(m->remaining > 1) {
                    m->remaining--;
                } else if(m->phase == PhaseWork && m->rest > 0 && m->round < m->rounds) {
                    m->phase = PhaseRest;
                    m->remaining = m->rest;
                    notify = true;
                } else if(m->round < m->rounds) {
                    m->phase = PhaseWork;
                    m->round++;
                    m->remaining = app->settings.work;
                    notify = true;
                } else {
                    m->phase = PhaseDone;
                    m->remaining = 0;
                    notify = finished = true;
                }
                vibrate = m->vibrate;
            }
        },
        true);

    if(notify) run_notify(app, vibrate, finished);
    if(finished) furi_timer_stop(app->tick);
    return true;
}

static void run_stop(App* app) {
    furi_timer_stop(app->tick);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

static void run_start(App* app) {
    with_view_model(
        app->run_view,
        RunModel * m,
        {
            m->phase = PhaseWork;
            m->round = 1;
            m->rounds = app->settings.rounds;
            m->remaining = app->settings.work;
            m->rest = app->settings.rest;
            m->vibrate = app->settings.vibrate;
        },
        true);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    run_notify(app, app->settings.vibrate, false);
    furi_timer_start(app->tick, furi_kernel_get_tick_frequency());
    view_dispatcher_switch_to_view(app->dispatcher, ViewRun);
}

static uint32_t run_back(void* ctx) {
    App* app = ctx;
    run_stop(app); // Back at any point (running or done) cancels and returns to the menu
    return ViewMenu;
}

/* ------------------------------------------------------------- menu / app */

static void menu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    switch(index) {
    case MenuStart:
        run_start(app);
        break;
    case MenuSettings:
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        break;
    }
}

static uint32_t exit_cb(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}

static uint32_t to_menu_cb(void* ctx) {
    UNUSED(ctx);
    return ViewMenu;
}

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

    SettingsLoadResult r = settings_load(app->storage, &app->settings);
    app->storage_ok = r != SettingsLoadStorageError;
    app->block_save = r == SettingsLoadNewer;
    if(r == SettingsLoadMigrated) {
        // Old (v1) values were kept; rewrite once in the new format.
        app->dirty = true;
        if(settings_save(app->storage, &app->settings)) app->dirty = false;
    }

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, run_custom_event);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_cb, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_cb, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_cb, app);
    view_set_previous_callback(submenu_get_view(app->menu), exit_cb);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_view = view_alloc();
    view_allocate_model(app->settings_view, ViewModelTypeLocking, sizeof(SettingsModel));
    view_set_context(app->settings_view, app);
    view_set_draw_callback(app->settings_view, settings_draw);
    view_set_input_callback(app->settings_view, settings_input);
    view_set_previous_callback(app->settings_view, settings_back);
    with_view_model(
        app->settings_view,
        SettingsModel * m,
        {
            m->settings = &app->settings;
            m->notice = settings_notice(app, r);
        },
        false);
    view_dispatcher_add_view(app->dispatcher, ViewSettings, app->settings_view);

    app->run_view = view_alloc();
    view_allocate_model(app->run_view, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->run_view, app);
    view_set_draw_callback(app->run_view, run_draw);
    view_set_previous_callback(app->run_view, run_back);
    view_dispatcher_add_view(app->dispatcher, ViewRun, app->run_view);

    app->about = widget_alloc();
    widget_add_text_box_element(
        app->about,
        0,
        0,
        128,
        64,
        AlignLeft,
        AlignTop,
        "\e#Interval Timer\e#\nWork/rest rounds with\noptional vibration.\nSettings: SD card\napps_data/interval_timer",
        false);
    view_set_previous_callback(widget_get_view(app->about), to_menu_cb);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->tick = furi_timer_alloc(run_tick_cb, FuriTimerTypePeriodic, app);

    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    view_dispatcher_run(app->dispatcher);

    // Cleanup (also reached via Back from the menu)
    run_stop(app);
    furi_timer_free(app->tick);
    if(app->dirty) app_save(app, NULL);

    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewRun);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    submenu_free(app->menu);
    view_free(app->settings_view);
    view_free(app->run_view);
    widget_free(app->about);
    view_dispatcher_free(app->dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
