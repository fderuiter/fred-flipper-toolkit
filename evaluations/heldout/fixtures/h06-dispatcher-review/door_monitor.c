// Synthetic benchmark input for a FRED held-out task. MIT licensed, original.
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define TAG          "DoorMonitor"
#define PRESETS_PATH APP_DATA_PATH("presets.bin")
#define MAX_PRESETS  8
#define WORKER_BLINK (1u << 0)

typedef enum {
    DoorViewMenu,
    DoorViewMonitor,
} DoorView;

typedef enum {
    DoorSceneMenu,
    DoorSceneMonitor,
    DoorSceneCount,
} DoorScene;

typedef enum {
    DoorEventTick = 100,
    DoorEventDisarm,
} DoorEvent;

typedef struct {
    uint16_t disarm_minutes;
    uint8_t blink_count;
    uint8_t reserved;
} Preset;

typedef struct {
    Gui* gui;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    Submenu* submenu;
    TextBox* text_box;
    FuriTimer* timer;
    FuriThread* worker;
    volatile uint32_t open_count;
    uint32_t armed_seconds;
    Preset presets[MAX_PRESETS];
    uint32_t preset_count;
    uint32_t active_preset;
} DoorApp;

/* ---------- GPIO interrupt ---------- */

static void door_irq_callback(void* context) {
    DoorApp* app = context;
    if(!furi_hal_gpio_read(&gpio_ext_pa7)) return;
    app->open_count++;
    FURI_LOG_I(TAG, "door opened (%lu)", (unsigned long)app->open_count);
    furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_BLINK);
}

/* ---------- worker ---------- */

static int32_t door_worker(void* context) {
    DoorApp* app = context;
    while(true) {
        uint32_t flags = furi_thread_flags_wait(WORKER_BLINK, FuriFlagWaitAny, FuriWaitForever);
        if(flags & WORKER_BLINK) {
            uint8_t blinks = app->presets[app->active_preset].blink_count;
            for(uint8_t i = 0; i < blinks; i++) {
                notification_message(app->notifications, &sequence_blink_red_100);
                furi_delay_ms(200);
            }
        }
    }
    return 0;
}

/* ---------- timer ---------- */

static void door_timer_callback(void* context) {
    DoorApp* app = context;
    app->armed_seconds++;
    uint32_t limit = (uint32_t)app->presets[app->active_preset].disarm_minutes * 60u;
    if(limit && app->armed_seconds >= limit) {
        furi_timer_stop(app->timer);
        furi_timer_flush();
        view_dispatcher_send_custom_event(app->view_dispatcher, DoorEventDisarm);
        return;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, DoorEventTick);
}

/* ---------- scenes ---------- */

static void door_menu_callback(void* context, uint32_t index) {
    DoorApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void door_scene_menu_on_enter(void* context) {
    DoorApp* app = context;
    submenu_reset(app->submenu);
    submenu_add_item(app->submenu, "Arm", 0, door_menu_callback, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, DoorViewMenu);
}

static bool door_scene_menu_on_event(void* context, SceneManagerEvent event) {
    DoorApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == 0) {
        scene_manager_next_scene(app->scene_manager, DoorSceneMonitor);
        return true;
    }
    return false;
}

static void door_scene_menu_on_exit(void* context) {
    DoorApp* app = context;
    submenu_reset(app->submenu);
}

static void door_monitor_refresh(DoorApp* app) {
    char text[64];
    snprintf(
        text,
        sizeof(text),
        "Armed %lu s\nOpened %lu times",
        (unsigned long)app->armed_seconds,
        (unsigned long)app->open_count);
    text_box_set_text(app->text_box, text);
}

static void door_scene_monitor_on_enter(void* context) {
    DoorApp* app = context;
    FURI_LOG_I(TAG, "armed");
    app->armed_seconds = 0;
    door_monitor_refresh(app);
    furi_timer_start(app->timer, furi_ms_to_ticks(1000));
    view_dispatcher_switch_to_view(app->view_dispatcher, DoorViewMonitor);
}

static bool door_scene_monitor_on_event(void* context, SceneManagerEvent event) {
    DoorApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == DoorEventTick) {
        door_monitor_refresh(app);
        return true;
    }
    if(event.event == DoorEventDisarm) {
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    return false;
}

static void door_scene_monitor_on_exit(void* context) {
    DoorApp* app = context;
    furi_timer_stop(app->timer);
    text_box_reset(app->text_box);
}

static void (*const door_on_enter[])(void*) = {
    door_scene_menu_on_enter,
    door_scene_monitor_on_enter,
};
static bool (*const door_on_event[])(void*, SceneManagerEvent) = {
    door_scene_menu_on_event,
    door_scene_monitor_on_event,
};
static void (*const door_on_exit[])(void*) = {
    door_scene_menu_on_exit,
    door_scene_monitor_on_exit,
};
static const SceneManagerHandlers door_scene_handlers = {
    .on_enter_handlers = door_on_enter,
    .on_event_handlers = door_on_event,
    .on_exit_handlers = door_on_exit,
    .scene_num = DoorSceneCount,
};

static bool door_custom_callback(void* context, uint32_t event) {
    DoorApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool door_back_callback(void* context) {
    DoorApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

/* ---------- presets ---------- */

static void door_load_presets(DoorApp* app) {
    app->presets[0] = (Preset){.disarm_minutes = 30, .blink_count = 3};
    app->preset_count = 1;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PRESETS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint32_t count = 0;
        if(storage_file_read(file, &count, sizeof(count)) == sizeof(count)) {
            for(uint32_t i = 0; i < count; i++) {
                if(storage_file_read(file, &app->presets[i], sizeof(Preset)) != sizeof(Preset)) break;
            }
            app->preset_count = count;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

/* ---------- lifecycle ---------- */

static DoorApp* door_app_alloc(void) {
    DoorApp* app = malloc(sizeof(DoorApp));
    memset(app, 0, sizeof(DoorApp));
    door_load_presets(app);

    app->gui = furi_record_open(RECORD_GUI);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_dispatcher = view_dispatcher_alloc();
    app->scene_manager = scene_manager_alloc(&door_scene_handlers, app);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->view_dispatcher, door_custom_callback);
    view_dispatcher_set_navigation_event_callback(app->view_dispatcher, door_back_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->submenu = submenu_alloc();
    view_dispatcher_add_view(app->view_dispatcher, DoorViewMenu, submenu_get_view(app->submenu));
    app->text_box = text_box_alloc();
    view_dispatcher_add_view(app->view_dispatcher, DoorViewMonitor, text_box_get_view(app->text_box));

    app->timer = furi_timer_alloc(door_timer_callback, FuriTimerTypePeriodic, app);

    app->worker = furi_thread_alloc_ex("DoorWorker", 1024, door_worker, app);
    furi_thread_start(app->worker);

    furi_hal_gpio_init(&gpio_ext_pa7, GpioModeInterruptRiseFall, GpioPullUp, GpioSpeedLow);
    furi_hal_gpio_add_int_callback(&gpio_ext_pa7, door_irq_callback, app);
    return app;
}

static void door_app_free(DoorApp* app) {
    furi_hal_gpio_remove_int_callback(&gpio_ext_pa7);
    furi_hal_gpio_init_simple(&gpio_ext_pa7, GpioModeAnalog);

    furi_timer_free(app->timer);
    furi_thread_free(app->worker);

    view_dispatcher_free(app->view_dispatcher);
    view_dispatcher_remove_view(app->view_dispatcher, DoorViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, DoorViewMonitor);
    submenu_free(app->submenu);
    text_box_free(app->text_box);
    scene_manager_free(app->scene_manager);

    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t door_monitor_app(void* p) {
    UNUSED(p);
    DoorApp* app = door_app_alloc();
    scene_manager_next_scene(app->scene_manager, DoorSceneMenu);
    view_dispatcher_run(app->view_dispatcher);
    door_app_free(app);
    return 0;
}
