#include "settings.h"

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/widget.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>

#define SETTINGS_STEP_SECONDS 5

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewAbout,
    ViewRunning,
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

typedef struct {
    Phase phase;
    uint32_t remaining;
    uint32_t round;
    uint32_t rounds;
} RunModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    DialogsApp* dialogs;
    NotificationApp* notifications;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    VariableItemList* settings_list;
    VariableItem* work_item;
    VariableItem* rest_item;
    VariableItem* rounds_item;
    Widget* about;
    View* running;
    FuriTimer* timer;

    Settings settings;
    bool dirty;
} App;

static void show_message(App* app, const char* header, const char* text) {
    DialogMessage* msg = dialog_message_alloc();
    dialog_message_set_header(msg, header, 64, 2, AlignCenter, AlignTop);
    dialog_message_set_text(msg, text, 64, 32, AlignCenter, AlignCenter);
    dialog_message_set_buttons(msg, NULL, "OK", NULL);
    dialog_message_show(app->dialogs, msg);
    dialog_message_free(msg);
}

/* ---------------------------------------------------------------- running */

static void running_draw(Canvas* canvas, void* model) {
    RunModel* m = model;
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

    snprintf(buf, sizeof(buf), "%s  %lu/%lu", m->phase == PhaseWork ? "WORK" : "REST",
             (unsigned long)m->round, (unsigned long)m->rounds);
    canvas_draw_str_aligned(canvas, 64, 6, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(m->remaining / 60),
             (unsigned long)(m->remaining % 60));
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 58, AlignCenter, AlignCenter, "Back: cancel");
}

static bool running_input(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return false; // let Back reach the previous-view callback
}

static void buzz(App* app, const NotificationSequence* seq) {
    if(app->settings.vibrate) notification_message(app->notifications, seq);
}

// Called once per second from the timer service thread.
static void timer_tick(void* context) {
    App* app = context;
    with_view_model(
        app->running,
        RunModel * m,
        {
            if(m->phase != PhaseDone) {
                if(m->remaining > 0) m->remaining--;
                if(m->remaining == 0) {
                    if(m->phase == PhaseWork) {
                        if(m->round >= m->rounds) {
                            m->phase = PhaseDone;
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
                    if(m->phase == PhaseDone) {
                        buzz(app, &sequence_success);
                        notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
                    } else {
                        buzz(app, &sequence_single_vibro);
                    }
                }
            }
        },
        true);
}

static void running_start(App* app) {
    with_view_model(
        app->running,
        RunModel * m,
        {
            m->phase = PhaseWork;
            m->remaining = app->settings.work;
            m->round = 1;
            m->rounds = app->settings.rounds;
        },
        true);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    buzz(app, &sequence_single_vibro);
    furi_timer_start(app->timer, furi_kernel_get_tick_frequency());
}

static void running_stop(App* app) {
    furi_timer_stop(app->timer);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

static uint32_t running_back(void* context) {
    App* app = context;
    running_stop(app);
    return ViewMenu;
}

/* --------------------------------------------------------------- settings */

static void format_seconds(char* buf, size_t size, uint32_t v) {
    snprintf(buf, size, "%lus", (unsigned long)v);
}

static uint32_t step_value(uint32_t v, int32_t delta, int32_t step, uint32_t min, uint32_t max) {
    int32_t n = (int32_t)v + delta * step;
    if(n < (int32_t)min) n = (int32_t)min;
    if(n > (int32_t)max) n = (int32_t)max;
    return (uint32_t)n;
}

/* The ranges (up to 600) exceed what one VariableItem can index, so each
 * stepper item has 3 positions with the middle one as "rest" position: moving
 * left/right yields a delta, then the item is re-centred. */
static void work_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    int32_t delta = (int32_t)variable_item_get_current_value_index(item) - 1;
    app->settings.work = step_value(
        app->settings.work, delta, SETTINGS_STEP_SECONDS, SETTINGS_WORK_MIN, SETTINGS_WORK_MAX);
    app->dirty = true;
    char buf[12];
    format_seconds(buf, sizeof(buf), app->settings.work);
    variable_item_set_current_value_index(item, 1);
    variable_item_set_current_value_text(item, buf);
}

static void rest_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    int32_t delta = (int32_t)variable_item_get_current_value_index(item) - 1;
    app->settings.rest = step_value(
        app->settings.rest, delta, SETTINGS_STEP_SECONDS, SETTINGS_REST_MIN, SETTINGS_REST_MAX);
    app->dirty = true;
    char buf[12];
    format_seconds(buf, sizeof(buf), app->settings.rest);
    variable_item_set_current_value_index(item, 1);
    variable_item_set_current_value_text(item, buf);
}

static void rounds_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    int32_t delta = (int32_t)variable_item_get_current_value_index(item) - 1;
    app->settings.rounds =
        step_value(app->settings.rounds, delta, 1, SETTINGS_ROUNDS_MIN, SETTINGS_ROUNDS_MAX);
    app->dirty = true;
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->settings.rounds);
    variable_item_set_current_value_index(item, 1);
    variable_item_set_current_value_text(item, buf);
}

static void vibrate_changed(VariableItem* item) {
    App* app = variable_item_get_context(item);
    uint8_t idx = variable_item_get_current_value_index(item);
    app->settings.vibrate = idx == 1;
    app->dirty = true;
    variable_item_set_current_value_text(item, idx ? "On" : "Off");
}

static void settings_build(App* app) {
    VariableItemList* list = app->settings_list;
    variable_item_list_reset(list);
    char buf[12];

    app->work_item = variable_item_list_add(list, "Work", 3, work_changed, app);
    variable_item_set_current_value_index(app->work_item, 1);
    format_seconds(buf, sizeof(buf), app->settings.work);
    variable_item_set_current_value_text(app->work_item, buf);

    app->rest_item = variable_item_list_add(list, "Rest", 3, rest_changed, app);
    variable_item_set_current_value_index(app->rest_item, 1);
    format_seconds(buf, sizeof(buf), app->settings.rest);
    variable_item_set_current_value_text(app->rest_item, buf);

    app->rounds_item = variable_item_list_add(list, "Rounds", 3, rounds_changed, app);
    variable_item_set_current_value_index(app->rounds_item, 1);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)app->settings.rounds);
    variable_item_set_current_value_text(app->rounds_item, buf);

    VariableItem* vib = variable_item_list_add(list, "Vibrate", 2, vibrate_changed, app);
    variable_item_set_current_value_index(vib, app->settings.vibrate ? 1 : 0);
    variable_item_set_current_value_text(vib, app->settings.vibrate ? "On" : "Off");
}

static uint32_t settings_back(void* context) {
    App* app = context;
    if(app->dirty) {
        if(settings_save(app->storage, &app->settings)) {
            app->dirty = false;
        } else {
            show_message(
                app, "Save failed", "Check the SD card.\nSettings apply until you exit.");
            app->dirty = false; // don't nag again; user can change them again to retry
        }
    }
    return ViewMenu;
}

/* ------------------------------------------------------------------- menu */

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        running_start(app);
        view_dispatcher_switch_to_view(app->dispatcher, ViewRunning);
        break;
    case MenuSettings:
        settings_build(app); // rebuild so positions/text reflect current values
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    case MenuAbout:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        break;
    }
}

static uint32_t to_menu(void* context) {
    UNUSED(context);
    return ViewMenu;
}

/* -------------------------------------------------------------------- app */

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->gui = furi_record_open(RECORD_GUI);

    SettingsLoadResult load = settings_load(app->storage, &app->settings);

    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    // No previous callback on the menu: Back exits the app.
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_list = variable_item_list_alloc();
    settings_build(app);
    view_set_context(variable_item_list_get_view(app->settings_list), app);
    view_set_previous_callback(variable_item_list_get_view(app->settings_list), settings_back);
    view_dispatcher_add_view(
        app->dispatcher, ViewSettings, variable_item_list_get_view(app->settings_list));

    app->about = widget_alloc();
    widget_add_text_box_element(
        app->about,
        0,
        0,
        128,
        64,
        AlignCenter,
        AlignCenter,
        "\e#Interval Timer\e#\nWork / rest rounds with\noptional vibration.\n"
        "Settings: /ext/apps_data/\ninterval_timer/settings.txt",
        false);
    view_set_previous_callback(widget_get_view(app->about), to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->running = view_alloc();
    view_allocate_model(app->running, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->running, app);
    view_set_draw_callback(app->running, running_draw);
    view_set_input_callback(app->running, running_input);
    view_set_previous_callback(app->running, running_back);
    view_dispatcher_add_view(app->dispatcher, ViewRunning, app->running);

    app->timer = furi_timer_alloc(timer_tick, FuriTimerTypePeriodic, app);

    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);

    if(load == SettingsLoadNoStorage) {
        show_message(app, "No SD card", "Using default settings.\nChanges won't be saved.");
    } else if(load == SettingsLoadRepaired) {
        show_message(app, "Settings invalid", "Defaults used for\nunreadable values.");
    }

    view_dispatcher_run(app->dispatcher);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);

    view_dispatcher_remove_view(app->dispatcher, ViewRunning);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_free(app->running);
    widget_free(app->about);
    variable_item_list_free(app->settings_list);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
