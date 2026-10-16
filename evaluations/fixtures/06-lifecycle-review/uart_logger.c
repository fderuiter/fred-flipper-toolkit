// Synthetic benchmark input for FRED evaluation task 06. MIT licensed, original.
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define TAG "UartLogger"
#define LINE_MAX 64
#define EVENT_QUEUE_LEN 4
#define CAL_PATH APP_DATA_PATH("calibration.bin")

typedef struct {
    uint32_t ticks;
} Model;

typedef enum {
    EventTypeInput,
    EventTypeLine,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} Event;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    NotificationApp* notifications;
    FuriMessageQueue* events;
    FuriMessageQueue* rx_queue;
    FuriMutex* mutex;
    FuriThread* worker;
    FuriTimer* timer;
    FuriHalSerialHandle* serial;
    Model* model;
    char line[LINE_MAX];
    size_t line_len;
    bool running;
    uint32_t* calibration;
    uint32_t calibration_count;
} App;

typedef struct {
    uint8_t* data;
    size_t size;
} RxChunk;

static void draw_callback(Canvas* canvas, void* context) {
    App* app = context;
    char uptime[24];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    snprintf(uptime, sizeof(uptime), "up %lu s", (unsigned long)(app->model->ticks / 10));
    canvas_draw_str(canvas, 2, 12, uptime);
    canvas_draw_str(canvas, 2, 30, app->line);
}

static void input_callback(InputEvent* input_event, void* context) {
    App* app = context;
    Event event = {.type = EventTypeInput, .input = *input_event};
    furi_message_queue_put(app->events, &event, FuriWaitForever);
}

static void tick_callback(void* context) {
    App* app = context;
    app->model->ticks++;
    view_port_update(app->view_port);
}

static void rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    App* app = context;
    if(event & FuriHalSerialRxEventData) {
        RxChunk chunk;
        chunk.data = malloc(8);
        chunk.size = 0;
        while(furi_hal_serial_async_rx_available(handle) && chunk.size < 8) {
            chunk.data[chunk.size++] = furi_hal_serial_async_rx(handle);
        }
        furi_message_queue_put(app->rx_queue, &chunk, 0);
    }
}

static int32_t worker_thread(void* context) {
    App* app = context;
    RxChunk chunk;
    while(true) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        bool running = app->running;
        furi_mutex_release(app->mutex);
        if(!running) break;

        if(furi_message_queue_get(app->rx_queue, &chunk, furi_ms_to_ticks(100)) != FuriStatusOk) {
            continue;
        }
        for(size_t i = 0; i < chunk.size; i++) {
            char c = (char)chunk.data[i];
            if(c == '\n' || app->line_len == LINE_MAX - 1) {
                app->line[app->line_len] = '\0';
                app->line_len = 0;
                Event event = {.type = EventTypeLine};
                furi_message_queue_put(app->events, &event, 0);
            } else {
                app->line[app->line_len++] = c;
            }
        }
        free(chunk.data);
    }
    return 0;
}

static uint32_t read_u32(File* file) {
    uint32_t value = 0;
    storage_file_read(file, &value, sizeof(value));
    return value;
}

static void load_calibration(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CAL_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint32_t count = read_u32(file);
        app->calibration = malloc(count * sizeof(uint32_t));
        app->calibration_count = count;
        storage_file_read(file, app->calibration, count * sizeof(uint32_t));
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->model = malloc(sizeof(Model));
    app->model->ticks = 0;
    app->running = true;

    app->events = furi_message_queue_alloc(EVENT_QUEUE_LEN, sizeof(Event));
    app->rx_queue = furi_message_queue_alloc(32, sizeof(RxChunk));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    load_calibration(app);

    app->timer = furi_timer_alloc(tick_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, furi_ms_to_ticks(100));

    app->worker = furi_thread_alloc_ex("UartLoggerWorker", 1024, worker_thread, app);
    furi_thread_start(app->worker);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_hal_serial_init(app->serial, 115200);
    furi_hal_serial_async_rx_start(app->serial, rx_callback, app, false);
    return app;
}

static void app_free(App* app) {
    furi_timer_stop(app->timer);
    free(app->model);

    furi_hal_serial_async_rx_stop(app->serial);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    app->running = false;
    furi_thread_join(app->worker);
    furi_mutex_release(app->mutex);
    furi_thread_free(app->worker);

    view_port_enabled_set(app->view_port, false);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);

    furi_record_close(RECORD_NOTIFICATION);
    notification_message(app->notifications, &sequence_success);

    furi_timer_free(app->timer);
    furi_message_queue_free(app->rx_queue);
    furi_message_queue_free(app->events);
    furi_mutex_free(app->mutex);
    free(app->calibration);
    free(app);
}

int32_t uart_logger_app(void* p) {
    UNUSED(p);
    App* app = app_alloc();
    Event event;
    bool exit = false;
    while(!exit) {
        if(furi_message_queue_get(app->events, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(event.type == EventTypeInput && event.input.key == InputKeyBack &&
           event.input.type == InputTypeShort) {
            exit = true;
        } else if(event.type == EventTypeLine) {
            notification_message(app->notifications, &sequence_blink_green_10);
            view_port_update(app->view_port);
            FURI_LOG_I(TAG, "line received");
        }
    }
    app_free(app);
    return 0;
}
