#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <expansion/expansion.h>

#include "soil_parser.h"

#define SOIL_BAUD       115200
#define RX_STREAM_SIZE  512
#define WORKER_STOP     (1U << 0)

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    FuriHalSerialHandle* serial;
    SoilParser parser; // guarded by mutex
    volatile uint32_t rx_dropped; // bytes lost to a full stream buffer (ISR writes)
} SoilApp;

// Runs in interrupt context: only move bytes, no parsing, no blocking.
static void serial_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    SoilApp* app = context;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->rx_stream, &byte, 1, 0) != 1) {
            app->rx_dropped++;
        }
    }
    // Framing/overrun/noise flags carry no data byte; the parser's CRC check
    // catches any resulting corruption.
}

static int32_t worker_fn(void* context) {
    SoilApp* app = context;
    uint8_t chunk[64];
    uint32_t seen_dropped = 0;

    while(!(furi_thread_flags_get() & WORKER_STOP)) {
        size_t got = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), 50);
        uint32_t dropped = app->rx_dropped;

        if(got > 0 || dropped != seen_dropped) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(got > 0) soil_parser_feed(&app->parser, chunk, got);
            if(dropped != seen_dropped) {
                app->parser.errors += dropped - seen_dropped;
                seen_dropped = dropped;
            }
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }
    }
    return 0;
}

static void draw_cb(Canvas* canvas, void* context) {
    SoilApp* app = context;
    char line[32];

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    const bool have = app->parser.have_reading;
    const uint16_t moisture = app->parser.moisture;
    const int16_t temp = app->parser.temperature_dc;
    const uint32_t ok = app->parser.frames_ok;
    const uint32_t errors = app->parser.errors;
    furi_mutex_release(app->mutex);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil sensor");

    canvas_set_font(canvas, FontSecondary);
    if(have) {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", moisture / 10, moisture % 10);
        canvas_draw_str(canvas, 2, 26, line);
        const int16_t t_abs = temp < 0 ? -temp : temp;
        snprintf(
            line, sizeof(line), "Temp: %s%d.%d C", temp < 0 ? "-" : "", t_abs / 10, t_abs % 10);
        canvas_draw_str(canvas, 2, 38, line);
    } else {
        canvas_draw_str(canvas, 2, 26, "Waiting for data...");
    }
    snprintf(line, sizeof(line), "OK: %lu  Errors: %lu", ok, errors);
    canvas_draw_str(canvas, 2, 52, line);
    canvas_draw_str(canvas, 2, 62, "Back: exit");
}

static void input_cb(InputEvent* event, void* context) {
    SoilApp* app = context;
    furi_message_queue_put(app->input_queue, event, 0);
}

int32_t soil_link_app(void* p) {
    UNUSED(p);

    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(SoilApp));
    soil_parser_reset(&app->parser);
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);

    // The expansion service may own the USART header; take it over for the
    // duration of the app and hand it back on exit.
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    bool serial_ok = false;
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 is the default framing
        furi_hal_serial_async_rx_start(app->serial, serial_rx_cb, app, false);
        serial_ok = true;
    }

    app->worker = furi_thread_alloc_ex("SoilLinkRx", 1024, worker_fn, app);
    if(serial_ok) furi_thread_start(app->worker);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->input_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.type == InputTypeShort && event.key == InputKeyBack) running = false;
        }
    }

    // Shutdown order: stop the source (ISR callback), then the consumer, then the UI.
    if(serial_ok) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP);
        furi_thread_join(app->worker);
        furi_hal_serial_deinit(app->serial);
    }
    if(app->serial) furi_hal_serial_control_release(app->serial);
    furi_thread_free(app->worker);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    furi_stream_buffer_free(app->rx_stream);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
