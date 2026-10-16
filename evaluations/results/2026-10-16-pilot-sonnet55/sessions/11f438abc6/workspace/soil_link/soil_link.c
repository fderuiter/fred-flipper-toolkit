#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <expansion/expansion.h>

#include "soil_proto.h"

#define RX_BUF_SIZE 256
#define WORKER_STOP (1U << 0)

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* mutex; // guards parser result fields + rx_overruns
    SoilParser parser;
    uint32_t rx_overruns;

    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
} SoilLink;

// Runs in interrupt context: only move bytes, no parsing/allocation.
static void serial_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilLink* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->rx_stream, &b, 1, 0) != 1) {
            app->rx_overruns++; // stream full; counted as an error by the UI
        }
    }
    if(event & (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError |
                FuriHalSerialRxEventOverrunError)) {
        app->rx_overruns++;
    }
}

static int32_t worker_thread(void* ctx) {
    SoilLink* app = ctx;
    uint8_t chunk[64];
    for(;;) {
        // Wake on data (or timeout) and drain in chunks so bursts are not lost.
        size_t n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), 50);
        if(n > 0) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(size_t i = 0; i < n; i++) soil_parser_feed(&app->parser, chunk[i]);
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
        }
        if(furi_thread_flags_get() & WORKER_STOP) break;
    }
    return 0;
}

static void draw_cb(Canvas* canvas, void* ctx) {
    SoilLink* app = ctx;
    FuriString* s = furi_string_alloc();

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    bool have = app->parser.have_reading;
    uint16_t m = app->parser.moisture;
    int16_t t = app->parser.temp_dc;
    uint32_t errors = app->parser.errors + app->rx_overruns;
    uint32_t frames = app->parser.frames;
    furi_mutex_release(app->mutex);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil Link 115200 8N1");
    canvas_set_font(canvas, FontSecondary);
    if(have) {
        furi_string_printf(s, "Moisture: %u.%u %%", m / 10, m % 10);
        canvas_draw_str(canvas, 2, 26, furi_string_get_cstr(s));
        int16_t at = t < 0 ? -t : t;
        furi_string_printf(s, "Temp: %s%d.%d C", t < 0 ? "-" : "", at / 10, at % 10);
        canvas_draw_str(canvas, 2, 38, furi_string_get_cstr(s));
    } else {
        canvas_draw_str(canvas, 2, 26, "Waiting for data...");
    }
    furi_string_printf(s, "Frames: %lu  Errors: %lu", frames, errors);
    canvas_draw_str(canvas, 2, 52, furi_string_get_cstr(s));
    canvas_draw_str(canvas, 2, 62, "Back: exit");
    furi_string_free(s);
}

static void input_cb(InputEvent* event, void* ctx) {
    SoilLink* app = ctx;
    furi_message_queue_put(app->input_queue, event, 0);
}

int32_t soil_link_app(void* p) {
    UNUSED(p);
    SoilLink* app = malloc(sizeof(SoilLink));
    memset(app, 0, sizeof(*app));
    soil_parser_reset(&app->parser);
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->rx_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);

    // Stop the expansion service from claiming the USART header (restored on exit).
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    // Receive-only; this app does not touch the 5V rail or any GPIO output.
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(app->serial);
    furi_hal_serial_init(app->serial, 115200); // HAL default framing is 8N1

    app->worker = furi_thread_alloc_ex("SoilLinkRx", 1024, worker_thread, app);
    furi_thread_start(app->worker);
    furi_hal_serial_async_rx_start(app->serial, serial_rx_cb, app, false);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent ev;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->input_queue, &ev, FuriWaitForever) == FuriStatusOk) {
            if(ev.key == InputKeyBack && (ev.type == InputTypeShort || ev.type == InputTypeLong)) {
                running = false;
            }
        }
    }

    // Teardown in reverse order: UI, RX source, worker, serial, buffers.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    furi_hal_serial_async_rx_stop(app->serial);
    furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP);
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    furi_stream_buffer_free(app->rx_stream);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
