#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <dialogs/dialogs.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define TAG "IntervalTimer"

#define SETTINGS_FILETYPE "Interval Timer Settings"
#define SETTINGS_VERSION  2U
#define SETTINGS_PATH     APP_DATA_PATH("settings.txt")
#define SETTINGS_TMP_PATH APP_DATA_PATH("settings.tmp")

#define WORK_MIN   5U
#define WORK_MAX   600U
#define REST_MIN   0U
#define REST_MAX   600U
#define ROUNDS_MIN 1U
#define ROUNDS_MAX 99U

#define WORK_DEFAULT   30U
#define REST_DEFAULT   10U
#define ROUNDS_DEFAULT 8U
#define VIBRATE_DEFAULT true

typedef enum {
    ViewMenu,
    ViewSettings,
    ViewRunning,
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
    uint8_t row;
    Settings values;
} SettingsModel;

typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* settings_view;
    View* running_view;
    Widget* about;
    NotificationApp* notifications;
    DialogsApp* dialogs;
    FuriTimer* timer;

    Settings settings;
    bool dirty;
} App;

/* ---------- Settings persistence ---------- */

static void settings_defaults(Settings* s) {
    s->work = WORK_DEFAULT;
    s->rest = REST_DEFAULT;
    s->rounds = ROUNDS_DEFAULT;
    s->vibrate = VIBRATE_DEFAULT;
}

/* Reads a uint32 key; keeps the existing value if the key is missing,
 * unparsable, or outside [min, max]. */
static void read_uint_ranged(FlipperFormat* ff, const char* key, uint32_t* out, uint32_t min, uint32_t max) {
    uint32_t v = 0;
    flipper_format_rewind(ff);
    if(flipper_format_read_uint32(ff, key, &v, 1) && v >= min && v <= max) {
        *out = v;
    } else {
        FURI_LOG_W(TAG, "Setting %s missing or invalid, using default %lu", key, *out);
    }
}

static bool settings_save(Settings* s) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = false;
    FlipperFormat* ff = flipper_format_file_alloc(storage);

    storage_simply_mkdir(storage, APP_DATA_PATH(""));

    do {
        // Write to a temp file first so a failure never destroys the old settings
        if(!flipper_format_file_open_always(ff, SETTINGS_TMP_PATH)) break;
        if(!flipper_format_write_header_cstr(ff, SETTINGS_FILETYPE, SETTINGS_VERSION)) break;
        if(!flipper_format_write_uint32(ff, "Work", &s->work, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rest", &s->rest, 1)) break;
        if(!flipper_format_write_uint32(ff, "Rounds", &s->rounds, 1)) break;
        if(!flipper_format_write_bool(ff, "Vibrate", &s->vibrate, 1)) break;
        ok = true;
    } while(false);

    // Close before rename; closing flushes the data
    flipper_format_free(ff);

    if(ok) {
        storage_common_remove(storage, SETTINGS_PATH);
        FS_Error err = storage_common_rename(storage, SETTINGS_TMP_PATH, SETTINGS_PATH);
        ok = (err == FSE_OK);
    }
    if(!ok) {
        storage_common_remove(storage, SETTINGS_TMP_PATH);
        FURI_LOG_E(TAG, "Failed to save settings");
    }

    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* Loads settings. Always leaves *s with valid values. Returns true if a
 * migration from an older format happened (caller should re-save). */
static bool settings_load(Settings* s) {
    settings_defaults(s);
    bool migrated = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();

    do {
        // No SD card, missing file, or unreadable file: keep defaults
        if(!flipper_format_file_open_existing(ff, SETTINGS_PATH)) break;

        uint32_t version = 0;
        if(!flipper_format_read_header(ff, filetype, &version)) {
            FURI_LOG_W(TAG, "Bad header, using defaults");
            break;
        }
        if(!furi_string_equal(filetype, SETTINGS_FILETYPE) || version == 0) {
            FURI_LOG_W(TAG, "Wrong filetype/version, using defaults");
            break;
        }

        // Fields present in every version
        read_uint_ranged(ff, "Work", &s->work, WORK_MIN, WORK_MAX);
        read_uint_ranged(ff, "Rest", &s->rest, REST_MIN, REST_MAX);

        if(version >= 2) {
            // Newer-than-known versions are read best effort: unknown keys are ignored
            read_uint_ranged(ff, "Rounds", &s->rounds, ROUNDS_MIN, ROUNDS_MAX);
            bool vib = false;
            flipper_format_rewind(ff);
            if(flipper_format_read_bool(ff, "Vibrate", &vib, 1)) s->vibrate = vib;
        }

        // Only upgrade v1 files. A future-version file is left untouched until
        // the user changes a setting, so a downgrade does not eat its extra data.
        migrated = (version < SETTINGS_VERSION);
    } while(false);

    furi_string_free(filetype);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return migrated;
}

static void app_save_settings_with_feedback(App* app) {
    if(!settings_save(&app->settings)) {
        DialogMessage* msg = dialog_message_alloc();
        dialog_message_set_header(msg, "Save failed", 64, 2, AlignCenter, AlignTop);
        dialog_message_set_text(
            msg,
            "Could not write settings\nto the SD card.\nChanges apply until exit.",
            64,
            32,
            AlignCenter,
            AlignCenter);
        dialog_message_set_buttons(msg, NULL, "OK", NULL);
        dialog_message_show(app->dialogs, msg);
        dialog_message_free(msg);
    } else {
        app->dirty = false;
    }
}

/* ---------- Settings view ---------- */

static const char* const settings_labels[] = {"Work (s)", "Rest (s)", "Rounds", "Vibrate"};

static void settings_draw(Canvas* canvas, void* _model) {
    SettingsModel* model = _model;
    const Settings* s = &model->values;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Settings");
    canvas_set_font(canvas, FontSecondary);

    char buf[16];
    for(uint8_t i = 0; i < 4; i++) {
        uint8_t y = 24 + i * 12;
        if(i == model->row) {
            canvas_draw_box(canvas, 0, y - 10, 128, 12);
            canvas_set_color(canvas, ColorWhite);
        }
        canvas_draw_str(canvas, 4, y, settings_labels[i]);
        switch(i) {
        case 0:
            snprintf(buf, sizeof(buf), "< %lu >", s->work);
            break;
        case 1:
            snprintf(buf, sizeof(buf), "< %lu >", s->rest);
            break;
        case 2:
            snprintf(buf, sizeof(buf), "< %lu >", s->rounds);
            break;
        default:
            snprintf(buf, sizeof(buf), "< %s >", s->vibrate ? "On" : "Off");
            break;
        }
        canvas_draw_str_aligned(canvas, 124, y, AlignRight, AlignBottom, buf);
        canvas_set_color(canvas, ColorBlack);
    }
}

static uint32_t clamp_step(uint32_t v, int32_t delta, uint32_t min, uint32_t max) {
    int64_t n = (int64_t)v + delta;
    if(n < (int64_t)min) n = min;
    if(n > (int64_t)max) n = max;
    return (uint32_t)n;
}

static bool settings_input(InputEvent* event, void* context) {
    App* app = context;
    if(event->key == InputKeyBack) return false; // let dispatcher navigate back

    bool press = event->type == InputTypeShort || event->type == InputTypeRepeat;
    if(!press) return true;

    int32_t step = (event->type == InputTypeRepeat) ? 10 : 1;

    with_view_model(
        app->settings_view,
        SettingsModel * model,
        {
            switch(event->key) {
            case InputKeyUp:
                model->row = (model->row + 3) % 4;
                break;
            case InputKeyDown:
                model->row = (model->row + 1) % 4;
                break;
            case InputKeyLeft:
            case InputKeyRight: {
                int32_t d = (event->key == InputKeyRight) ? step : -step;
                Settings* s = &model->values;
                switch(model->row) {
                case 0:
                    s->work = clamp_step(s->work, d, WORK_MIN, WORK_MAX);
                    break;
                case 1:
                    s->rest = clamp_step(s->rest, d, REST_MIN, REST_MAX);
                    break;
                case 2:
                    s->rounds = clamp_step(s->rounds, d, ROUNDS_MIN, ROUNDS_MAX);
                    break;
                default:
                    s->vibrate = !s->vibrate;
                    break;
                }
                app->settings = *s;
                app->dirty = true;
                break;
            }
            default:
                break;
            }
        },
        true);
    return true;
}

static void settings_exit(void* context) {
    App* app = context;
    if(app->dirty) app_save_settings_with_feedback(app);
}

/* ---------- Running view ---------- */

static void running_draw(Canvas* canvas, void* _model) {
    RunModel* m = _model;
    char buf[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(m->phase == PhaseDone) {
        canvas_draw_str_aligned(canvas, 64, 24, AlignCenter, AlignCenter, "Done!");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Press Back to exit");
        return;
    }

    canvas_draw_str_aligned(
        canvas, 64, 6, AlignCenter, AlignCenter, m->phase == PhaseWork ? "WORK" : "REST");
    canvas_set_font(canvas, FontBigNumbers);
    snprintf(buf, sizeof(buf), "%lu:%02lu", m->remaining / 60, m->remaining % 60);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "Round %lu/%lu", m->round, m->rounds);
    canvas_draw_str_aligned(canvas, 64, 52, AlignCenter, AlignCenter, buf);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Back: cancel");
}

static void notify_phase_change(App* app, Phase phase) {
    // Sound/light cue is always given; vibration only if enabled
    if(phase == PhaseDone) {
        notification_message(app->notifications, &sequence_success);
    } else {
        notification_message(app->notifications, &sequence_blink_blue_100);
    }
    if(app->settings.vibrate) {
        notification_message(
            app->notifications,
            phase == PhaseDone ? &sequence_double_vibro : &sequence_single_vibro);
    }
}

static void timer_callback(void* context) {
    App* app = context;
    Phase announce = PhaseWork;
    bool changed = false;
    bool stop = false;

    with_view_model(
        app->running_view,
        RunModel * m,
        {
            if(m->phase != PhaseDone) {
                if(m->remaining > 0) m->remaining--;
                if(m->remaining == 0) {
                    if(m->phase == PhaseWork) {
                        if(m->round >= m->rounds) {
                            m->phase = PhaseDone;
                            stop = true;
                        } else if(app->settings.rest > 0) {
                            m->phase = PhaseRest;
                            m->remaining = app->settings.rest;
                        } else {
                            m->round++;
                            m->remaining = app->settings.work;
                        }
                    } else {
                        m->round++;
                        m->phase = PhaseWork;
                        m->remaining = app->settings.work;
                    }
                    announce = m->phase;
                    changed = true;
                }
            }
        },
        true);

    if(stop) furi_timer_stop(app->timer);
    if(changed) notify_phase_change(app, announce);
}

static void running_enter(void* context) {
    App* app = context;
    with_view_model(
        app->running_view,
        RunModel * m,
        {
            m->phase = PhaseWork;
            m->round = 1;
            m->rounds = app->settings.rounds;
            m->remaining = app->settings.work;
        },
        true);
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
    furi_timer_start(app->timer, furi_kernel_get_tick_frequency());
}

static void running_exit(void* context) {
    App* app = context;
    furi_timer_stop(app->timer);
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
}

static bool running_input(InputEvent* event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return false; // Back (short) falls through to the previous-view callback
}

/* ---------- Navigation ---------- */

static uint32_t nav_to_menu(void* context) {
    UNUSED(context);
    return ViewMenu;
}

static uint32_t nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void menu_callback(void* context, uint32_t index) {
    App* app = context;
    switch(index) {
    case MenuStart:
        running_enter(app);
        view_dispatcher_switch_to_view(app->dispatcher, ViewRunning);
        break;
    case MenuSettings:
        with_view_model(
            app->settings_view,
            SettingsModel * m,
            {
                m->values = app->settings;
                m->row = 0;
            },
            true);
        view_dispatcher_switch_to_view(app->dispatcher, ViewSettings);
        break;
    default:
        view_dispatcher_switch_to_view(app->dispatcher, ViewAbout);
        break;
    }
}

/* ---------- Entry point ---------- */

int32_t interval_timer_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->dialogs = furi_record_open(RECORD_DIALOGS);

    bool migrated = settings_load(&app->settings);
    if(migrated) {
        // Upgrade the old file in place; values were preserved by the load.
        // A failure here is harmless: the old file stays valid and is retried next time.
        settings_save(&app->settings);
    }

    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->menu = submenu_alloc();
    submenu_add_item(app->menu, "Start", MenuStart, menu_callback, app);
    submenu_add_item(app->menu, "Settings", MenuSettings, menu_callback, app);
    submenu_add_item(app->menu, "About", MenuAbout, menu_callback, app);
    view_set_previous_callback(submenu_get_view(app->menu), nav_exit);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));

    app->settings_view = view_alloc();
    view_allocate_model(app->settings_view, ViewModelTypeLocking, sizeof(SettingsModel));
    view_set_context(app->settings_view, app);
    view_set_draw_callback(app->settings_view, settings_draw);
    view_set_input_callback(app->settings_view, settings_input);
    view_set_exit_callback(app->settings_view, settings_exit);
    view_set_previous_callback(app->settings_view, nav_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewSettings, app->settings_view);

    app->running_view = view_alloc();
    view_allocate_model(app->running_view, ViewModelTypeLocking, sizeof(RunModel));
    view_set_context(app->running_view, app);
    view_set_draw_callback(app->running_view, running_draw);
    view_set_input_callback(app->running_view, running_input);
    view_set_exit_callback(app->running_view, running_exit);
    view_set_previous_callback(app->running_view, nav_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewRunning, app->running_view);

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
        "Settings are stored on\n"
        "the SD card in\n"
        "/data (app data).\n"
        "Back cancels a run.");
    view_set_previous_callback(widget_get_view(app->about), nav_to_menu);
    view_dispatcher_add_view(app->dispatcher, ViewAbout, widget_get_view(app->about));

    app->timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, app);

    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    view_dispatcher_run(app->dispatcher);

    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->dispatcher, ViewSettings);
    view_dispatcher_remove_view(app->dispatcher, ViewRunning);
    view_dispatcher_remove_view(app->dispatcher, ViewAbout);
    submenu_free(app->menu);
    view_free(app->settings_view);
    view_free(app->running_view);
    widget_free(app->about);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_NOTIFICATION);
    free(app);
    return 0;
}
