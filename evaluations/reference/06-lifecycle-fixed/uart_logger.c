// Reviewer-side corrected version of the FRED task 06 input. MIT licensed, original.
// Each change is labelled with the defect it removes (see oracles/06-lifecycle-review.json).
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#define TAG             "UartLogger"
#define LINE_MAX        64
#define EVENT_QUEUE_LEN 8
#define RX_STREAM_BYTES 256
#define CAL_PATH        APP_DATA_PATH("calibration.bin")
#define CAL_MAX_COUNT   256u /* D8: explicit bound */
#define WORKER_STOP     (1u << 0)

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
    FuriStreamBuffer* rx_stream; /* D3: preallocated, IRQ-safe handoff */
    FuriMutex* mutex;
    FuriThread* worker;
    FuriTimer* timer;
    FuriHalSerialHandle* serial;
    Model model; /* guarded by mutex */
    char line[LINE_MAX]; /* published line, guarded by mutex (D6) */
    uint32_t rx_overflow; /* __atomic from IRQ */
    uint32_t* calibration;
    uint32_t calibration_count;
} App;

static void draw_callback(Canvas* canvas, void* context) {
    App* app = context;
    char uptime[24];
    char line[LINE_MAX];
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    snprintf(uptime, sizeof(uptime), "up %lu s", (unsigned long)(app->model.ticks / 10));
    memcpy(line, app->line, sizeof(line)); /* D6: snapshot under lock */
    furi_mutex_release(app->mutex);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 12, uptime);
    canvas_draw_str(canvas, 2, 30, line);
}

static void input_callback(InputEvent* input_event, void* context) {
    App* app = context;
    Event event = {.type = EventTypeInput, .input = *input_event};
    /* D4: GUI thread never blocks; line events are dropped before input can starve
     * because the worker only posts when at least two slots are free. */
    furi_message_queue_put(app->events, &event, 0);
}

static void tick_callback(void* context) {
    App* app = context;
    /* Timer daemon: short, nonblocking. */
    if(furi_mutex_acquire(app->mutex, 0) == FuriStatusOk) {
        app->model.ticks++;
        furi_mutex_release(app->mutex);
    }
    view_port_update(app->view_port);
}

static void rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    App* app = context;
    if(!(event & FuriHalSerialRxEventData)) return;
    uint8_t chunk[16];
    size_t n = 0;
    while(furi_hal_serial_async_rx_available(handle) && n < sizeof(chunk)) {
        chunk[n++] = furi_hal_serial_async_rx(handle);
    }
    /* D3: no heap in IRQ; zero timeout; overflow counted. */
    size_t sent = furi_stream_buffer_send(app->rx_stream, chunk, n, 0);
    if(sent < n) __atomic_fetch_add(&app->rx_overflow, (uint32_t)(n - sent), __ATOMIC_RELAXED);
}

static int32_t worker_thread(void* context) {
    App* app = context;
    char pending[LINE_MAX];
    size_t pending_len = 0;
    uint8_t buf[32];
    while(!(furi_thread_flags_get() & WORKER_STOP)) {
        size_t n =
            furi_stream_buffer_receive(app->rx_stream, buf, sizeof(buf), furi_ms_to_ticks(100));
        for(size_t i = 0; i < n; i++) {
            char c = (char)buf[i];
            if(c == '\n' || pending_len == LINE_MAX - 1) {
                pending[pending_len] = '\0';
                pending_len = 0;
                furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
                memcpy(app->line, pending, sizeof(app->line)); /* D6 */
                furi_mutex_release(app->mutex);
                if(furi_message_queue_get_space(app->events) >= 2) {
                    Event event = {.type = EventTypeLine};
                    furi_message_queue_put(app->events, &event, 0);
                }
            } else {
                pending[pending_len++] = c;
            }
        }
    }
    return 0;
}

static bool read_exact(File* file, void* dst, size_t size) {
    return storage_file_read(file, dst, size) == size;
}

static void load_calibration(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, CAL_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint32_t count = 0;
        /* D8: bound count before multiplying, check every read length. */
        if(read_exact(file, &count, sizeof(count)) && count > 0 && count <= CAL_MAX_COUNT) {
            uint32_t* table = malloc(count * sizeof(uint32_t));
            if(read_exact(file, table, count * sizeof(uint32_t))) {
                app->calibration = table;
                app->calibration_count = count;
            } else {
                free(table);
                FURI_LOG_W(TAG, "calibration truncated");
            }
        } else {
            FURI_LOG_W(TAG, "calibration rejected");
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

static App* app_alloc(void) {
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->events = furi_message_queue_alloc(EVENT_QUEUE_LEN, sizeof(Event));
    app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_BYTES, 1);
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

    /* Unchecked acquire was an additional defect: the handle is NULL when busy. */
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        furi_hal_serial_init(app->serial, 115200);
        furi_hal_serial_async_rx_start(app->serial, rx_callback, app, false);
    } else {
        FURI_LOG_W(TAG, "USART busy");
    }
    return app;
}

static void app_free(App* app) {
    /* 1. Stop producers. D1: free the timer (stop + daemon flush) before anything
     *    its callback touches is released. */
    furi_timer_free(app->timer);
    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
    }
    /* 2. D5: signal and join the worker without holding a lock it needs. */
    furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP);
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);

    /* 3. D2: detach from GUI before freeing the ViewPort. */
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);

    /* 4. D7: use the borrowed record before closing it. */
    notification_message(app->notifications, &sequence_success);
    furi_record_close(RECORD_NOTIFICATION);

    furi_stream_buffer_free(app->rx_stream);
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
