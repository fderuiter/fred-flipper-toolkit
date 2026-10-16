// Soil Link: shows the latest reading and an error count from a soil sensor on the USART header
// (pins 13 TX / 14 RX, 115200 8N1). RX only; the app never drives GPIO or supplies power.
// Logic level of the sensor board is UNCONFIRMED: use 3.3 V logic only.

#include <furi.h>
#include <furi_hal_serial.h>
#include <gui/gui.h>
#include <expansion/expansion.h>

#include <stdio.h>

#include "soil_frame.h"

#define TAG "SoilLink"

#define SOIL_BAUD         115200
#define RX_STREAM_SIZE    1024 // ~89 ms of line-rate data at 11.5 kB/s; worker polls every 50 ms
#define WORKER_POLL_MS    50
#define WORKER_CHUNK      64

#define FLAG_EXIT (1U << 0)

typedef enum {
    LinkStateStarting,
    LinkStateRunning,
    LinkStateBusy, // serial handle unavailable
} LinkState;

typedef struct {
    LinkState link;
    bool has_reading;
    SoilReading reading;
    uint32_t frames;
    uint32_t errors; // parser errors + UART errors + RX overflow
} Model;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex; // guards model
    Model model;
    FuriEventFlag* flags;

    Expansion* expansion;
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;

    volatile bool stop; // set by main thread, read by worker
    // Written only from the RX IRQ callback, read by worker (32-bit aligned, tolerable for
    // a monotonic diagnostic counter).
    volatile uint32_t isr_errors;
} App;

// ---- RX IRQ callback. Context: interrupt. Zero-timeout ops only, no heap, no logging. ----
static void rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* context) {
    App* app = context;
    if(event & (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError |
                FuriHalSerialRxEventOverrunError)) {
        app->isr_errors++;
    }
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            uint8_t b = furi_hal_serial_async_rx(handle);
            if(furi_stream_buffer_send(app->rx_stream, &b, 1, 0) != 1) {
                app->isr_errors++; // stream full: byte dropped, counted
            }
        }
    }
}

// ---- Worker thread: stream -> parser -> model. ----
static int32_t worker_thread(void* context) {
    App* app = context;
    SoilParser parser;
    soil_parser_reset(&parser);
    uint32_t reported_errors = 0;
    uint8_t chunk[WORKER_CHUNK];

    while(!app->stop) {
        size_t n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), WORKER_POLL_MS);
        bool changed = false;
        SoilReading latest = {0};
        bool got = false;

        for(size_t i = 0; i < n; i++) {
            if(soil_parser_feed(&parser, chunk[i], &latest)) got = true;
        }

        uint32_t total_errors = parser.error_count + app->isr_errors;
        if(got || total_errors != reported_errors) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(got) {
                app->model.reading = latest;
                app->model.has_reading = true;
            }
            app->model.frames = parser.frame_count;
            app->model.errors = total_errors;
            furi_mutex_release(app->mutex);
            reported_errors = total_errors;
            changed = true;
        }
        // One redraw per received chunk, not per byte.
        if(changed) view_port_update(app->view_port);
    }
    return 0;
}

// ---- GUI callbacks. Context: GUI thread. Snapshot + draw only. ----
static void draw_callback(Canvas* canvas, void* context) {
    App* app = context;
    Model m;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    m = app->model;
    furi_mutex_release(app->mutex);

    char line[32];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil Link 115200 8N1");
    canvas_set_font(canvas, FontSecondary);

    if(m.link == LinkStateBusy) {
        canvas_draw_str(canvas, 2, 28, "USART busy (in use by");
        canvas_draw_str(canvas, 2, 40, "another service)");
        canvas_draw_str(canvas, 2, 62, "Back: exit");
        return;
    }

    if(m.has_reading) {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", m.reading.moisture / 10, m.reading.moisture % 10);
        canvas_draw_str(canvas, 2, 26, line);
        int t = m.reading.temperature_dc;
        snprintf(line, sizeof(line), "Temp: %s%d.%d C", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
        canvas_draw_str(canvas, 2, 38, line);
    } else {
        canvas_draw_str(canvas, 2, 30, "Waiting for data...");
    }
    snprintf(line, sizeof(line), "Frames: %lu", (unsigned long)m.frames);
    canvas_draw_str(canvas, 2, 50, line);
    snprintf(line, sizeof(line), "Errors: %lu", (unsigned long)m.errors);
    canvas_draw_str(canvas, 2, 61, line);
}

static void input_callback(InputEvent* event, void* context) {
    App* app = context;
    // Back never goes through a queue, so it cannot be dropped.
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        furi_event_flag_set(app->flags, FLAG_EXIT);
    }
}

int32_t soil_link_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->model.link = LinkStateStarting;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->flags = furi_event_flag_alloc();

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // The expansion service may own the USART header; disable it while we run, re-enable on exit.
    app->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(app->expansion);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!app->serial) {
        FURI_LOG_E(TAG, "USART busy");
        app->model.link = LinkStateBusy;
    } else {
        app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
        app->worker = furi_thread_alloc_ex("SoilLinkRx", 1024, worker_thread, app);
        furi_thread_start(app->worker);

        furi_hal_serial_init(app->serial, SOIL_BAUD);
        furi_hal_serial_async_rx_start(app->serial, rx_callback, app, true);
        app->model.link = LinkStateRunning;
    }
    view_port_update(app->view_port);

    furi_event_flag_wait(app->flags, FLAG_EXIT, FuriFlagWaitAny, FuriWaitForever);

    // ---- Teardown: producers first, then worker, then what they touch. ----
    app->stop = true;
    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial); // no more IRQ callbacks
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
        app->serial = NULL;
        furi_thread_join(app->worker); // worker wakes within WORKER_POLL_MS
        furi_thread_free(app->worker);
        furi_stream_buffer_free(app->rx_stream);
    }

    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);

    expansion_enable(app->expansion);
    furi_record_close(RECORD_EXPANSION);

    furi_event_flag_free(app->flags);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
