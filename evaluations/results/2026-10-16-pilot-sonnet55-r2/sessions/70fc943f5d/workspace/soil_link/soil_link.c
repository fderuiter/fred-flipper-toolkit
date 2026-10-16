#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <input/input.h>
#include <expansion/expansion.h>
#include <stdio.h>

#include "soil_frame.h"

#define RX_BUF_SIZE 512 // absorbs bursts after the sensor wakes up
#define SOIL_BAUD   115200

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex;
    FuriMessageQueue* input_queue;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    FuriHalSerialHandle* serial;

    SoilParser parser;
    SoilReading reading;
    bool have_reading;
    volatile bool running;
} SoilApp;

// Runs in interrupt context: only push bytes, never parse or lock.
static void soil_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(app->rx_stream, &byte, 1, 0);
    }
}

static int32_t soil_worker(void* ctx) {
    SoilApp* app = ctx;
    uint8_t chunk[64];
    while(app->running) {
        size_t n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), 50);
        if(!n) continue;
        bool updated = false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        for(size_t i = 0; i < n; i++) {
            SoilReading r;
            if(soil_parser_feed(&app->parser, chunk[i], &r)) {
                app->reading = r; // keep only the latest of a burst
                app->have_reading = true;
            }
            updated = true;
        }
        furi_mutex_release(app->mutex);
        if(updated) view_port_update(app->view_port);
    }
    return 0;
}

static void soil_draw(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    char line[48];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Soil sensor (UART 115200)");
    canvas_set_font(canvas, FontSecondary);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->have_reading) {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", app->reading.moisture / 10, app->reading.moisture % 10);
        canvas_draw_str(canvas, 2, 28, line);
        int t = app->reading.temperature_dc;
        snprintf(line, sizeof(line), "Temp: %s%d.%d C", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
        canvas_draw_str(canvas, 2, 40, line);
    } else {
        canvas_draw_str(canvas, 2, 28, "Waiting for data...");
    }
    snprintf(line, sizeof(line), "Errors: %lu  OK: %lu", (unsigned long)app->parser.error_count, (unsigned long)app->parser.ok_count);
    canvas_draw_str(canvas, 2, 56, line);
    furi_mutex_release(app->mutex);
}

static void soil_input(InputEvent* event, void* ctx) {
    SoilApp* app = ctx;
    furi_message_queue_put(app->input_queue, event, 0);
}

int32_t soil_link_app(void* p) {
    UNUSED(p);
    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(*app));
    soil_parser_reset(&app->parser);

    // The expansion protocol service owns the USART header by default.
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->rx_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    app->running = true;

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        // Default framing is 8N1.
        furi_hal_serial_init(app->serial, SOIL_BAUD);
        furi_hal_serial_async_rx_start(app->serial, soil_rx_callback, app, false);
    }

    app->worker = furi_thread_alloc_ex("SoilRx", 1024, soil_worker, app);
    furi_thread_start(app->worker);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, soil_draw, app);
    view_port_input_callback_set(app->view_port, soil_input, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent ev;
    while(furi_message_queue_get(app->input_queue, &ev, FuriWaitForever) == FuriStatusOk) {
        if(ev.type == InputTypeShort && ev.key == InputKeyBack) break;
    }

    // Teardown: stop RX first so the ISR no longer touches the stream buffer.
    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
    }
    app->running = false;
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    furi_stream_buffer_free(app->rx_stream);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);
    free(app);
    return 0;
}
