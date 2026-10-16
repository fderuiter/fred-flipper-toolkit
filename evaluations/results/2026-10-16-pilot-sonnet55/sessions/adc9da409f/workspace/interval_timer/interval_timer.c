#include "settings.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <gui/modules/popup.h>
#include <notification/notification_messages.h>

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
    EventPopupDone = 1,
} AppEvent;

typedef enum {
    PhaseWork,
    PhaseRest,
    PhaseDone,
} Phase;

typedef struct {
    Phase phase;
    uint32_t seconds_left;
    uint32_t round;
    uint32_t rounds;
} RunModel;

typedef struct {
    ViewDispatcher* dispatcher;
    Submenu* menu;
    VariableItemList* list;
    View* running;
    Widget* about;
    Popup* popup;
    FuriTimer* timer;
    NotificationApp* notifications;

    IntervalSettings settings;
    bool settings_read_only; // newer-format file on disk; never overwrite it
    ViewId popup_next;

} App;

// The settings list owns its view context, so its back handler cannot receive the App.
static App* g_app;

/* ---------- popup helper ---------- */

static void app_show_popup(App* app, const char* header, const char* text, ViewId next) {
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 4, AlignCenter, AlignTop);
    popup_set_text(app->popup, text, 64, 22, AlignCenter, AlignTop);
    popup_set_timeout(app->popup, 3000);
    popup_enable_timeout(app->popup);
    app->popup_next = next;
    view_dispatcher_switch_to_view(app->dispatcher, ViewPopup);
}

static void popup_done_callback(void* context) {
    App* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, EventPopupDone);
}

static bool custom_event_callback(void* context, uint32_t event) {
    App* app = context;
    if(event == EventPopupDone) {
        view_dispatcher_switch_to_view(app->dispatcher, app->popup_next);
        return true;
    }
    return false;
}

/* ---------- navigation ---------- */

static uint32_t nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t nav_to_menu(void* context) {
    UNUSED(context);
    return ViewMenu;
}

// Leaving settings persists them. On failure the user is told, and the in-memory values
// still apply for this session.
static uint32_t nav_from_settings(void* context) {
    UNUSED(context);
    App* app = g_app;
    if(app->settings_read_only) {
        app_show_popup(app, "Not saved", "Settings file is from a\nnewer version.", ViewMenu);
        return ViewPopup;
    }
    if(settings_save(&app->settings) != SettingsSaveOk) {
        app_show_popup(app, "Not saved", "SD card error.\nSettings kept until exit.", ViewMenu);
        return ViewPopup;
    }
    return ViewMenu;
}

/* ---------- settings screen ---------- */

// VariableItemList stores the value count in a uint8_t, which cannot hold 0..600. Each item
// therefore has 3 slots and is re-centred on slot 1 after every change; moving left or
// right off the centre is read as -1 / +1.
#define CENTER 1

static int32_t step_of(VariableItem* item) {
    int32_t step = (int32_t)variable_item_get_current_value_index(item) - CENTER;
    variable_item_set_current_value_index(item, CENTER);
    return step;
}

static uint32_t clamp_step(uint32_t v, int32_t step, uint32_t min, uint32_t max) {
    int64_t n = (int64_t)v + step;
    if(n < (int64_t)min) n = min;
    if(n > (int64_t)max) n = max;
    return (uint32_t)n;
}

static void show_number(VariableItem* item, uint32_t v, const char* unit) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu%s", (unsigned long)v, unit);
    variable_item_set_current_value_text(item, buf);
}

static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.work_s = clamp_step(app->settings.work_s, step_of(item), WORK_MIN, WORK_MAX);
    show_number(item, app->settings.work_s, "s");
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rest_s = clamp_step(app->settings.rest_s, step_of(item), REST_MIN, REST_MAX);
    show_number(item, app->settings.rest_s, "s");
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    app->settings.rounds =
        clamp_step(app->settings.rounds, step_of(item), ROUNDS_MIN, ROUNDS_MAX);
    show_number(item, app->settings.rounds, "");
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.vibrate = (idx == 1);
    variable_item_set_current_value_text(item, idx ? "On" : "Off");
}

static void settings_build_list(App* app) {
    VariableItem* item;
    variable_item_list_reset(app->list);

    item = variable_item_list_add(app->list, "Work", 3, work_changed, app);
    variable_item_set_current_value_index(item, CENTER);
    show_number(item, app->settings.work_s, "s");

    item = variable_item_list_add(app->list, "Rest", 3, rest_changed, app);
    variable_item_set_current_value_index(item, CENTER);
    show_number(item, app->settings.rest_s, "s");

    item = variable_item_list_add(app->list, "Rounds", 3, rounds_changed, app);
    variable_item_set_current_value_index(item, CENTER);
    show_number(item, app->settings.rounds, "");

    item = variable_item_list_add(app->list, "Vibrate", 2, vibrate_changed, app);
    variable_item_set_current_value_index(item, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.vibrate ? "On" : "Off");
}

/* ---------- running screen ---------- */

static void running_draw(Canvas* canvas, void* model_ptr) {
    RunModel* m = model_ptr;
    char buf[24];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(m->phase == PhaseDone) {
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 56, AlignCenter, AlignBottom, "Back: menu");
        return;
    }

    canvas_set_font(canvas, FontPrimary);
    snprintf(buf, sizeof(buf), "Round %lu/%lu", (unsigned long)m->round, (unsigned long)m->rounds);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, buf);
    canvas_draw_str_aligned(
        canvas, 64, 18, AlignCenter, AlignTop, m->phase == PhaseWork ? "WORK" : "REST");

    canvas_set_font(canvas, FontBigNumbers);
    snprintf(
        buf,
        sizeof(buf),
        "%lu:%02lu",
        (unsigned long)(m->seconds_left / 60),
        (unsigned long)(m->seconds_left % 60));
    canvas_draw_str_aligned(canvas, 64, 40, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "Back: cancel");
}

static void running_begin_phase(RunModel* m, Phase phase, uint32_t seconds) {
    m->phase = phase;
    m->seconds_left = seconds;
}

typedef enum {
    TickNone,
    TickPhaseChange,
    TickFinished,
} TickEvent;

static void running_tick(void* context) {
    App* app = context;
    TickEvent ev = TickNone;
    const IntervalSettings* cfg = &app->settings;

    with_view_model(
        app->running,
        RunModel * m,
        {
            if(m->phase != PhaseDone && m->seconds_left > 0) {
                m->seconds_left--;
                if(m->seconds_left == 0) {
                    ev = TickPhaseChange;
                    if(m->phase == PhaseWork && m->round >= m->rounds) {
                        m->phase = PhaseDone;
                        ev = TickFinished;
                    } else if(m->phase == PhaseWork && cfg->rest_s > 0) {
                        running_begin_phase(m, PhaseRest, cfg->rest_s);
                    } else {
                        m->round++;
                        running_begin_phase(m, PhaseWork, cfg->work_s);
                    }
                }
            }
        },
        ev != TickNone);

    if(ev != TickNone && app->settings.vibrate) {
        notification_message(
            app->notifications,
            ev == TickFinished ? &sequence_double_vibro : &sequence_single_vibro);
    }
}

static void running_enter(void* context) {
    App* app = context;
    with_view_model(
        app->running,
        RunModel * m,
        {
            m->round = 1;
            m->rounds = app->settings.rounds;
            running_begin_phase(m, PhaseWork, app->settings.work_s);
        },
        true);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    furi_timer_start(app->timer, furi_kernel_get_tick_frequency());
}

// Runs on Back (via nav_to_menu) and on any other way of leaving the view.
static void running_exit(void* context) {
    App* app = context;
    furi_timer_stop(app->timer);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

/* ---------- menu ---------- */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        view_dispatcher_switch_to_view(app->dispatcher, ViewRunning);
        break;
    case MenuSettings:
        settings_build_list(app);
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        break;
    }
}

/* ---------- lifecycle ---------- */

static const char* load_message(SettingsLoadResult r) {
    switch(r) {
    case SettingsLoadRecovered:
        return "Settings file damaged.\nDefaults used for bad values.";
    case SettingsLoadFuture:
        return "Settings are from a newer\nversion. Changes won't save.";
    case SettingsLoadStorageError:
        return "SD card unavailable.\nSettings won't be saved.";
    default:
        return NULL;
    }
}

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    g_app = app;

    SettingsLoadResult loaded = settings_load(&app->settings);
    app->settings_read_only = (loaded == SettingsLoadFuture);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    Gui* gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, custom_event_callback);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), nav_exit);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->list = variable_item_list_alloc();
    view_set_previous_callback(variable_item_list_get_view(app->list), nav_from_settings);
    view_dispatcher_add_view(app->dispatcher, ViewSettings, variable_item_list_get_view(app->list));

    app->running = view_alloc();
    view_allocate_model(app->running, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->running, app);
    view_set_draw_callback(app->running, running_draw);
    view_set_enter_callback(app->running, running_enter);
    view_set_exit_callback(app->running, running_exit);
    view_set_previous_callback(app->running, nav_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewRunning, app->running);
    app->timer = furi_timer_alloc(running_tick, FuriTimerTypePeriodic, app);

    app->about = widget_alloc();
    widget_add_text_scroll_element(
        app->about,
        0,
        0,
        128,
        64,
        "\e#Interval Timer\e#\n"
        "Work/rest rounds with\n"
        "optional vibration.\n"
        "Settings: Work 5-600s,\n"
        "Rest 0-600s, Rounds 1-99.\n"
        "Saved on SD card in\n"
        "apps_data/interval_timer.\n"
        "Back cancels a running\n"
        "timer.");
    view_set_previous_callback(widget_get_view(app->about), nav_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->popup = popup_alloc();
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, popup_done_callback);
    view_set_previous_callback(popup_get_view(app->popup), nav_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewPopup, popup_get_view(app->popup));

    view_dispatcher_attach_to_gui(app->dispatcher, gui, ViewDispatcherTypeFullscreen);

    const char* msg = load_message(loaded);
    if(msg) {
        app_show_popup(app, "Settings", msg, ViewMenu);
    } else {
        view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    }
    view_dispatcher_run(app->dispatcher);

    furi_timer_free(app->timer);
    view_dispatcher_remove_view(app->dispatcher, ViewPopup);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewRunning);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    popup_free(app->popup);
    widget_free(app->about);
    view_free(app->running);
    variable_item_list_free(app->list);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
    return 0;
}
