// Reviewer-side reference solution for FRED task 03. MIT licensed, original.
// USART RX (IRQ) -> stream buffer -> worker (framing) -> mutex-guarded model -> ViewPort.
#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include "soil_frame.h"

#define TAG             "SoilUart"
#define BAUD            115200
#define RX_STREAM_BYTES 512 /* ~44 ms of line-rate data at 115200 8N1 */
#define WORKER_STOP     (1u << 0)

typedef struct {
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial;
    FuriThread* worker;
    ViewPort* view_port;
    uint32_t rx_overflow; /* written in IRQ with __atomic, read with __atomic */
    /* guarded by mutex */
    bool have_reading;
    SoilReading last;
    SoilStats stats;
} App;

static void rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    App* app = context;
    if(!(event & FuriHalSerialRxEventData)) return;
    uint8_t chunk[16];
    size_t n = 0;
    while(furi_hal_serial_async_rx_available(handle) && n < sizeof(chunk)) {
        chunk[n++] = furi_hal_serial_async_rx(handle);
    }
    /* IRQ context: zero timeout, no allocation, no logging. */
    size_t sent = furi_stream_buffer_send(app->rx_stream, chunk, n, 0);
    if(sent < n) __atomic_fetch_add(&app->rx_overflow, (uint32_t)(n - sent), __ATOMIC_RELAXED);
}

static void on_reading(const SoilReading* reading, void* context) {
    App* app = context;
    app->last = *reading;
    app->have_reading = true;
}

static int32_t worker(void* context) {
    App* app = context;
    SoilParser parser;
    soil_parser_reset(&parser);
    uint8_t buf[64];
    while(!(furi_thread_flags_get() & WORKER_STOP)) {
        size_t n =
            furi_stream_buffer_receive(app->rx_stream, buf, sizeof(buf), furi_ms_to_ticks(50));
        if(n == 0) continue;
        furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
        soil_parser_feed(&parser, buf, n, on_reading, app);
        app->stats = parser.stats;
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }
    return 0;
}

static void draw(Canvas* canvas, void* context) {
    App* app = context;
    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(!app->serial) {
        canvas_draw_str(canvas, 2, 12, "USART busy");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 26, "Another app or log owns it.");
        canvas_draw_str(canvas, 2, 38, "Press Back to exit.");
        return;
    }
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(app->have_reading) {
        snprintf(line, sizeof(line), "Moisture %u", app->last.moisture);
        canvas_draw_str(canvas, 2, 12, line);
        int t = app->last.temp_decicelsius;
        snprintf(
            line,
            sizeof(line),
            "Temp %s%d.%d C",
            t < 0 ? "-" : "",
            (t < 0 ? -t : t) / 10,
            (t < 0 ? -t : t) % 10);
        canvas_draw_str(canvas, 2, 26, line);
    } else {
        canvas_draw_str(canvas, 2, 12, "Waiting for data");
    }
    uint32_t errors = app->stats.bad_crc + app->stats.bad_length + app->stats.bad_payload;
    furi_mutex_release(app->mutex);
    canvas_set_font(canvas, FontSecondary);
    snprintf(
        line,
        sizeof(line),
        "errors %lu  overflow %lu",
        (unsigned long)errors,
        (unsigned long)__atomic_load_n(&app->rx_overflow, __ATOMIC_RELAXED));
    canvas_draw_str(canvas, 2, 44, line);
    canvas_draw_str(canvas, 2, 60, "Back: stop and exit");
}

static void input(InputEvent* event, void* context) {
    App* app = context;
    furi_message_queue_put(app->input_queue, event, 0);
}

int32_t fred_soil_uart_app(void* p) {
    UNUSED(p);
    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_BYTES, 1);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw, app);
    view_port_input_callback_set(app->view_port, input, app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app->view_port, GuiLayerFullscreen);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        app->worker = furi_thread_alloc_ex("SoilUartWorker", 1024, worker, app);
        furi_thread_start(app->worker);
        furi_hal_serial_init(app->serial, BAUD);
        furi_hal_serial_async_rx_start(app->serial, rx_callback, app, false);
    } else {
        FURI_LOG_W(TAG, "USART busy");
    }
    view_port_update(app->view_port);

    InputEvent event;
    while(furi_message_queue_get(app->input_queue, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.key == InputKeyBack && event.type == InputTypeShort) break;
    }

    /* Teardown: stop the IRQ producer, release the UART, stop and join the
     * worker (no lock held), then free what they used. */
    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
        furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    view_port_enabled_set(app->view_port, false);
    gui_remove_view_port(gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_stream_buffer_free(app->rx_stream);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
