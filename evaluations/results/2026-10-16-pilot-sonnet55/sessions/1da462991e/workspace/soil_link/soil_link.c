#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <expansion/expansion.h>

#include "soil_frame.h"

#define SOIL_BAUD       115200
#define RX_BUFFER_SIZE  512
#define WORKER_STOP_FLG (1U << 0)
#define WORKER_DATA_FLG (1U << 1)

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex;
    FuriStreamBuffer* rx_stream;
    FuriHalSerialHandle* serial;
    FuriThread* worker;

    // Protected by mutex
    bool have_reading;
    SoilReading reading;
    uint32_t frames;
    uint32_t errors;
    // Set from ISR when the stream buffer was full and bytes were lost
    volatile uint32_t overruns;
} SoilApp;

static void draw_cb(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil sensor (USART 115200)");
    canvas_set_font(canvas, FontSecondary);

    char line[40];
    if(app->have_reading) {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", app->reading.moisture / 10, app->reading.moisture % 10);
        canvas_draw_str(canvas, 2, 26, line);
        int t = app->reading.temperature_dc;
        snprintf(line, sizeof(line), "Temp: %s%d.%d C", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
        canvas_draw_str(canvas, 2, 38, line);
    } else {
        canvas_draw_str(canvas, 2, 26, "Waiting for data...");
    }
    snprintf(line, sizeof(line), "Frames: %lu  Errors: %lu", app->frames, app->errors + app->overruns);
    canvas_draw_str(canvas, 2, 52, line);
    canvas_draw_str(canvas, 2, 62, "Back: exit");

    furi_mutex_release(app->mutex);
}

static void input_cb(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, 0);
}

// Runs in interrupt context: only move bytes, no parsing, no blocking.
static void serial_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->rx_stream, &byte, 1, 0) != 1) app->overruns++;
        furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_DATA_FLG);
    }
    // Framing/overrun/noise flags are handled by the HAL; bad bytes surface as CRC errors.
}

static int32_t worker_fn(void* ctx) {
    SoilApp* app = ctx;
    SoilParser parser;
    soil_parser_reset(&parser);

    for(;;) {
        uint32_t flags = furi_thread_flags_wait(
            WORKER_STOP_FLG | WORKER_DATA_FLG, FuriFlagWaitAny, FuriWaitForever);
        if(flags & WORKER_STOP_FLG) break;

        // Drain everything queued so bursts are processed in one pass.
        uint8_t chunk[64];
        size_t n;
        bool changed = false;
        while((n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), 0)) > 0) {
            for(size_t i = 0; i < n; i++) {
                SoilReading r;
                bool got = soil_parser_feed(&parser, chunk[i], &r);
                furi_mutex_acquire(app->mutex, FuriWaitForever);
                if(got) {
                    app->reading = r; // keep only the newest of a burst
                    app->have_reading = true;
                }
                app->frames = parser.frame_count;
                app->errors = parser.error_count;
                furi_mutex_release(app->mutex);
                changed = true;
            }
        }
        if(changed) view_port_update(app->view_port);
    }
    return 0;
}

int32_t soil_link_app(void* p) {
    UNUSED(p);

    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(*app));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->rx_stream = furi_stream_buffer_alloc(RX_BUFFER_SIZE, 1);
    FuriMessageQueue* input_queue = furi_message_queue_alloc(4, sizeof(InputEvent));

    // The expansion service owns the USART header by default; release it while we run.
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    app->worker = furi_thread_alloc_ex("SoilRx", 1024, worker_fn, app);
    furi_thread_start(app->worker);

    // Only the UART RX/TX pins (13/14) are touched; no 5 V rail or other GPIO is enabled,
    // since the sensor board's logic level and supply are unconfirmed.
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_check(app->serial);
    furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 is the HAL default
    furi_hal_serial_async_rx_start(app->serial, serial_rx_cb, app, false);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, input_queue);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(input_queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.key == InputKeyBack && (event.type == InputTypeShort || event.type == InputTypeLong)) {
                running = false;
            }
        }
    }

    // Teardown: stop the source first, then the consumer, then UI.
    furi_hal_serial_async_rx_stop(app->serial);
    furi_hal_serial_deinit(app->serial);
    furi_hal_serial_control_release(app->serial);

    furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP_FLG);
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    furi_message_queue_free(input_queue);
    furi_stream_buffer_free(app->rx_stream);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
