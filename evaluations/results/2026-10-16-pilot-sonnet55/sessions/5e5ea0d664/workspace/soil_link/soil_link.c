#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#include "soil_parser.h"

#define SOIL_BAUD      115200
#define RX_BUF_SIZE    512
#define WORKER_STOP    (1U << 0)
#define WORKER_RX      (1U << 1)

typedef struct {
    FuriMutex* mutex;
    ViewPort* view_port;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    FuriHalSerialHandle* serial;
    SoilParser parser;
    SoilReading reading;
    bool have_reading;
    uint32_t frame_count;
} SoilApp;

// ISR-context callback: only move bytes into the stream buffer.
static void soil_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(app->rx_stream, &b, 1, 0);
        furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_RX);
    }
    // Overrun/framing/noise flags are not data; the parser's CRC covers corrupted bytes.
}

static int32_t soil_worker(void* ctx) {
    SoilApp* app = ctx;
    uint8_t chunk[64];
    for(;;) {
        uint32_t flags =
            furi_thread_flags_wait(WORKER_STOP | WORKER_RX, FuriFlagWaitAny, FuriWaitForever);
        if(flags & WORKER_STOP) break;

        size_t n;
        while((n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), 0)) > 0) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            for(size_t i = 0; i < n; i++) {
                SoilReading r;
                if(soil_parser_feed(&app->parser, chunk[i], &r)) {
                    app->reading = r;
                    app->have_reading = true;
                    app->frame_count++;
                }
            }
            furi_mutex_release(app->mutex);
        }
        view_port_update(app->view_port);
    }
    return 0;
}

static void soil_draw(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    char line[48];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 11, "Soil Link  115200 8N1");
    canvas_set_font(canvas, FontSecondary);

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->have_reading) {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", app->reading.moisture / 10, app->reading.moisture % 10);
        canvas_draw_str(canvas, 2, 26, line);
        int t = app->reading.temperature_dc;
        snprintf(line, sizeof(line), "Temp: %s%d.%d C", (t < 0) ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
        canvas_draw_str(canvas, 2, 38, line);
    } else {
        canvas_draw_str(canvas, 2, 26, "Waiting for data...");
    }
    snprintf(line, sizeof(line), "OK: %lu  Errors: %lu", app->frame_count, app->parser.error_count);
    canvas_draw_str(canvas, 2, 52, line);
    furi_mutex_release(app->mutex);
    canvas_draw_str(canvas, 2, 62, "Back: exit");
}

static void soil_input(InputEvent* event, void* ctx) {
    FuriMessageQueue* q = ctx;
    furi_message_queue_put(q, event, 0);
}

int32_t soil_link_app(void* p) {
    UNUSED(p);
    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(*app));
    soil_parser_reset(&app->parser);
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->rx_stream = furi_stream_buffer_alloc(RX_BUF_SIZE, 1);
    FuriMessageQueue* input_q = furi_message_queue_alloc(4, sizeof(InputEvent));

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, soil_draw, app);
    view_port_input_callback_set(app->view_port, soil_input, input_q);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app->view_port, GuiLayerFullscreen);

    app->worker = furi_thread_alloc_ex("SoilWorker", 1024, soil_worker, app);
    furi_thread_start(app->worker);

    // USART header (pins 13/14). Acquire fails if another service (e.g. expansion
    // protocol / CLI over UART) owns it; Flipper's expansion module setting must be off.
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 is the HAL default
        furi_hal_serial_async_rx_start(app->serial, soil_rx_cb, app, false);
    }

    InputEvent ev;
    while(furi_message_queue_get(input_q, &ev, FuriWaitForever) == FuriStatusOk) {
        if(ev.type == InputTypeShort && ev.key == InputKeyBack) break;
    }

    // Teardown order: stop RX callback before freeing what it uses.
    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
    }
    furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP);
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);

    gui_remove_view_port(gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(input_q);
    furi_stream_buffer_free(app->rx_stream);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
