#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/popup.h>
#include <gui/modules/widget.h>
#include <notification/notification_messages.h>

#include "settings.h"

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewRunning,
    ViewAbout,
    ViewPopup,
} ViewId;

typedef enum {
    MenuStart,
    MenuSettings,
    MenuAbout,
} MenuIndex;

typedef enum {
    PhaseWork,
    PhaseRest,
    PhaseDone,
} Phase;

#define TICK_MS 200

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* settings_view;
    View* running_view;
    Widget* about;
    Popup* popup;
    FuriTimer* timer;
    Settings settings;
} App;

/* ---------------------------------------------------------------- navigation */

static uint32_t to_menu(void* ctx) {
    UNUSED(ctx);
    return ViewMenu;
}

/* ------------------------------------------------------------------ settings */

typedef struct {
    Settings* settings; // points at App.settings, edited in place
    uint8_t row;
} SettingsModel;

#define SETTINGS_ROWS 4

static void settings_draw(Canvas* canvas, void* model) {
    SettingsModel* m = model;
    const Settings* s = m->settings;
    char buf[24];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Settings");
    canvas_set_font(canvas, FontSecondary);

    static const char* const labels[SETTINGS_ROWS] = {"Work (s)", "Rest (s)", "Rounds", "Vibrate"};
    for(uint8_t i = 0; i < SETTINGS_ROWS; i++) {
        uint8_t y = 24 + i * 12;
        switch(i) {
        case 0:
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)s->work);
            break;
        case 1:
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)s->rest);
            break;
        case 2:
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)s->rounds);
            break;
        default:
            snprintf(buf, sizeof(buf), "%s", s->vibrate ? "On" : "Off");
            break;
        }
        if(i == m->row) canvas_draw_str(canvas, 2, y, ">");
        canvas_draw_str(canvas, 12, y, labels[i]);
        canvas_draw_str_aligned(canvas, 100, y, AlignRight, AlignBottom, buf);
        if(i == m->row) {
            canvas_draw_str(canvas, 104, y, "<>");
        }
    }
}

static uint32_t clamp_add(uint32_t v, int32_t delta, uint32_t min, uint32_t max) {
    int32_t n = (int32_t)v + delta;
    if(n < (int32_t)min) n = (int32_t)min;
    if(n > (int32_t)max) n = (int32_t)max;
    return (uint32_t)n;
}

static bool settings_input(InputEvent* event, void* ctx) {
    View* view = ((App*)ctx)->settings_view;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return false;

    bool consumed = true;
    // Holding left/right accelerates: repeat events step by 10.
    int32_t step = (event->type == InputTypeRepeat) ? 10 : 1;

    with_view_model(
        view,
        SettingsModel * m,
        {
            Settings* s = m->settings;
            switch(event->key) {
            case InputKeyUp:
                m->row = (m->row + SETTINGS_ROWS - 1) % SETTINGS_ROWS;
                break;
            case InputKeyDown:
                m->row = (m->row + 1) % SETTINGS_ROWS;
                break;
            case InputKeyLeft:
            case InputKeyRight: {
                int32_t d = (event->key == InputKeyRight) ? step : -step;
                if(m->row == 0)
                    s->work = clamp_add(s->work, d, WORK_MIN, WORK_MAX);
                else if(m->row == 1)
                    s->rest = clamp_add(s->rest, d, REST_MIN, REST_MAX);
                else if(m->row == 2)
                    s->rounds = clamp_add(s->rounds, d, ROUNDS_MIN, ROUNDS_MAX);
                else if(event->type == InputTypeShort)
                    s->vibrate = !s->vibrate;
                break;
            }
            case InputKeyOk:
                if(m->row == 3 && event->type == InputTypeShort) s->vibrate = !s->vibrate;
                break;
            default:
                consumed = false; // Back falls through to the previous-callback
                break;
            }
        },
        true);
    return consumed;
}

// Back from settings: persist, and if the SD card refuses, tell the user.
static uint32_t settings_back(void* ctx) {
    App* app = ctx;
    if(settings_save(&app->settings)) return ViewMenu;
    popup_set_header(app->popup, "Not saved", 64, 14, AlignCenter, AlignCenter);
    popup_set_text(app->popup, "SD card unavailable.\nChanges last until exit.", 64, 36, AlignCenter, AlignCenter);
    return ViewPopup;
}

/* ------------------------------------------------------------------- running */

typedef struct {
    Phase phase;
    uint32_t round; // 1-based
    uint32_t total_rounds;
    uint32_t remaining; // seconds, rounded up
    uint32_t deadline; // furi tick at which the current phase ends
    uint32_t work_s;
    uint32_t rest_s;
    bool vibrate;
} RunModel;

static void run_draw(Canvas* canvas, void* model) {
    RunModel* m = model;
    char buf[24];
    canvas_set_font(canvas, FontPrimary);

    if(m->phase == PhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Press Back");
        return;
    }

    snprintf(buf, sizeof(buf), "Round %lu/%lu", (unsigned long)m->round, (unsigned long)m->total_rounds);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignCenter, buf);
    canvas_draw_str_aligned(
        canvas, 64, 20, AlignCenter, AlignCenter, m->phase == PhaseWork ? "WORK" : "REST");

    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)(m->remaining / 60), (unsigned long)(m->remaining % 60));
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 60, AlignCenter, AlignCenter, "Back: cancel");
}

static void run_buzz(App* app, const NotificationSequence* seq, bool vibrate) {
    if(vibrate) notification_message(app->notifications, seq);
}

static void run_begin_phase(RunModel* m, Phase phase, uint32_t seconds) {
    m->phase = phase;
    m->remaining = seconds;
    m->deadline = furi_get_tick() + seconds * 1000;
}

static void run_timer_cb(void* ctx) {
    App* app = ctx;
    with_view_model(
        app->running_view,
        RunModel * m,
        {
            if(m->phase != PhaseDone) {
                uint32_t now = furi_get_tick();
                // Tick arithmetic is wrap-safe via signed difference.
                int32_t left_ms = (int32_t)(m->deadline - now);
                if(left_ms > 0) {
                    m->remaining = (uint32_t)(left_ms + 999) / 1000;
                } else if(m->phase == PhaseWork) {
                    if(m->round >= m->total_rounds) {
                        m->phase = PhaseDone;
                        m->remaining = 0;
                        run_buzz(app, &sequence_double_vibro, m->vibrate);
                        notification_message(app->notifications, &sequence_success);
                    } else if(m->rest_s > 0) {
                        run_begin_phase(m, PhaseRest, m->rest_s);
                        run_buzz(app, &sequence_single_vibro, m->vibrate);
                    } else {
                        m->round++;
                        run_begin_phase(m, PhaseWork, m->work_s);
                        run_buzz(app, &sequence_single_vibro, m->vibrate);
                    }
                } else { // rest finished
                    m->round++;
                    run_begin_phase(m, PhaseWork, m->work_s);
                    run_buzz(app, &sequence_single_vibro, m->vibrate);
                }
            }
        },
        true);
}

static void run_enter(void* ctx) {
    App* app = ctx;
    with_view_model(
        app->running_view,
        RunModel * m,
        {
            m->round = 1;
            m->total_rounds = app->settings.rounds;
            m->work_s = app->settings.work;
            m->rest_s = app->settings.rest;
            m->vibrate = app->settings.vibrate;
            run_begin_phase(m, PhaseWork, m->work_s);
        },
        true);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    run_buzz(app, &sequence_single_vibro, app->settings.vibrate);
    furi_timer_start(app->timer, furi_ms_to_ticks(TICK_MS));
}

static void run_exit(void* ctx) {
    App* app = ctx;
    furi_timer_stop(app->timer);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

// Back is not consumed here, so the dispatcher uses the previous-callback (-> menu).
static bool run_input(InputEvent* event, void* ctx) {
    UNUSED(event);
    UNUSED(ctx);
    return false;
}

/* ---------------------------------------------------------------------- menu */

static void menu_cb(void* ctx, uint32_t index) {
    App* app = ctx;
    switch(index) {
    case MenuStart:
        view_dispatcher_switch_to_view(app->dispatcher, ViewRunning);
        break;
    case MenuSettings:
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        break;
    }
}

static void popup_done_cb(void* ctx) {
    App* app = ctx;
    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
}

/* ---------------------------------------------------------------------- main */

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    SettingsLoadResult load = settings_load(&app->settings);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_cb, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_cb, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_cb, app);
    view_set_previous_callback(submenu_get_view(app->menu), NULL); // Back exits the app
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_view = view_alloc();
    view_set_context(app->settings_view, app);
    view_allocate_model(app->settings_view, ViewModelTypeLocking, sizeof(SettingsModel));
    with_view_model(
        app->settings_view, SettingsModel * m, { m->settings = &app->settings; m->row = 0; }, false);
    view_set_draw_callback(app->settings_view, settings_draw);
    view_set_input_callback(app->settings_view, settings_input);
    view_set_previous_callback(app->settings_view, settings_back);
    view_dispatcher_add_view(app->dispatcher, ViewSettings, app->settings_view);

    app->running_view = view_alloc();
    view_set_context(app->running_view, app);
    view_allocate_model(app->running_view, ViewModelTypeLocking, sizeof(RunModel));
    view_set_draw_callback(app->running_view, run_draw);
    view_set_input_callback(app->running_view, run_input);
    view_set_enter_callback(app->running_view, run_enter);
    view_set_exit_callback(app->running_view, run_exit);
    view_set_previous_callback(app->running_view, to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewRunning, app->running_view);
    app->timer = furi_timer_alloc(run_timer_cb, FuriTimerTypePeriodic, app);

    app->about = widget_alloc();
    widget_add_string_multiline_element(
        app->about,
        64,
        32,
        AlignCenter,
        AlignCenter,
        FontSecondary,
        "Interval Timer v2\nWork / rest rounds\nSettings: SD card\n(apps_data/interval_timer)");
    view_set_previous_callback(widget_get_view(app->about), to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->popup = popup_alloc();
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, popup_done_cb);
    popup_set_timeout(app->popup, 2500);
    popup_enable_timeout(app->popup);
    view_set_previous_callback(popup_get_view(app->popup), to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewPopup, popup_get_view(app->popup));

    if(load == SettingsLoadPartial || load == SettingsLoadStorageError) {
        // Start usable with defaults, but say why the saved values are missing.
        if(load == SettingsLoadPartial) {
            popup_set_header(app->popup, "Settings damaged", 64, 14, AlignCenter, AlignCenter);
            popup_set_text(app->popup, "Defaults used for\ninvalid values.", 64, 36, AlignCenter, AlignCenter);
        } else {
            popup_set_header(app->popup, "No SD card", 64, 14, AlignCenter, AlignCenter);
            popup_set_text(app->popup, "Using defaults.\nSettings won't persist.", 64, 36, AlignCenter, AlignCenter);
        }
        view_dispatcher_switch_to_view(app->dispatcher, ViewPopup);
    } else {
        view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    }

    view_dispatcher_run(app->dispatcher);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    view_dispatcher_remove_view(app->dispatcher, ViewPopup);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewRunning);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    popup_free(app->popup);
    widget_free(app->about);
    view_free(app->running_view);
    view_free(app->settings_view);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
    return 0;
}
