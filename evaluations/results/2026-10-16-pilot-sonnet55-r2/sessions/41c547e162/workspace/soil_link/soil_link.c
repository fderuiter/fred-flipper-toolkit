#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <expansion/expansion.h>
#include <stdio.h>

#include "soil_parser.h"

#define SOIL_BAUD 115200

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* mutex; // guards parser
    SoilParser parser;

    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    volatile bool running;
} SoilApp;

static void rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(app->rx_stream, &b, 1, 0);
    }
    // Overrun/framing/noise flags are ignored here; the CRC catches bad data.
}

static int32_t worker_thread(void* ctx) {
    SoilApp* app = ctx;
    uint8_t chunk[64];
    while(app->running) {
        // Drains bursts in chunks; times out so we can notice app->running.
        size_t n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), 50);
        if(!n) continue;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        for(size_t i = 0; i < n; i++) soil_parser_feed(&app->parser, chunk[i]);
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }
    return 0;
}

static void draw_callback(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    char line[48];
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    SoilParser p = app->parser;
    furi_mutex_release(app->mutex);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Soil Link 115200 8N1");
    canvas_set_font(canvas, FontSecondary);
    if(p.has_reading) {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", p.moisture / 10, p.moisture % 10);
        canvas_draw_str(canvas, 2, 28, line);
        int t = p.temperature;
        snprintf(line, sizeof(line), "Temp: %s%d.%d C", t < 0 ? "-" : "", abs(t) / 10, abs(t) % 10);
        canvas_draw_str(canvas, 2, 40, line);
    } else {
        canvas_draw_str(canvas, 2, 28, "Waiting for data...");
    }
    snprintf(line, sizeof(line), "OK: %lu  Errors: %lu", p.frames_ok, p.errors);
    canvas_draw_str(canvas, 2, 56, line);
}

static void input_callback(InputEvent* event, void* ctx) {
    SoilApp* app = ctx;
    furi_message_queue_put(app->input_queue, event, 0);
}

int32_t soil_link_app(void* p) {
    UNUSED(p);
    SoilApp* app = malloc(sizeof(SoilApp));
    soil_parser_reset(&app->parser);
    app->running = true;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->rx_stream = furi_stream_buffer_alloc(512, 1);

    // Release the USART header from the expansion-module protocol service.
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(app->serial);
    furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 is the default framing
    furi_hal_serial_async_rx_start(app->serial, rx_callback, app, false);

    app->worker = furi_thread_alloc_ex("SoilRx", 1024, worker_thread, app);
    furi_thread_start(app->worker);

    InputEvent ev;
    while(true) {
        if(furi_message_queue_get(app->input_queue, &ev, FuriWaitForever) == FuriStatusOk) {
            if(ev.type == InputTypeShort && ev.key == InputKeyBack) break;
        }
    }

    // Teardown in reverse order: stop the source, then the consumer.
    furi_hal_serial_async_rx_stop(app->serial);
    app->running = false;
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);

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
