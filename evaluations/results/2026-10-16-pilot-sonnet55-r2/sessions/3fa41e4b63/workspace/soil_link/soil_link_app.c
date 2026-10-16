#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#include "soil_frame.h"

#define TAG              "SoilLink"
#define SOIL_BAUD        115200
#define RX_STREAM_SIZE   1024 // ~89 ms of line-rate bytes at 115200 8N1
#define WORKER_POLL_MS   50
#define EVT_BACK         (1U << 0)
#define WORKER_STOP_FLAG (1U << 0)

typedef enum {
    LinkStateStarting,
    LinkStateRunning,
    LinkStateBusy, // serial interface owned by someone else
} LinkState;

typedef struct {
    LinkState state;
    bool have_reading;
    SoilReading reading;
    uint32_t frames_ok;
    uint32_t errors;
    uint32_t rx_overflow; // bytes the ISR could not queue
} SoilModel;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* model_mutex; // guards model
    SoilModel model;
    FuriEventFlag* events; // Back from input callback -> main loop

    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream; // producer: serial ISR, consumer: worker
    FuriThread* worker;
    volatile uint32_t isr_overflow; // written only by ISR, read by worker (single 32-bit word)
    SoilParser parser; // worker thread only
} SoilApp;

// ---- UI (GUI thread): snapshot the model under the mutex, then draw ----

static void draw_cb(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    SoilModel m;
    furi_mutex_acquire(app->model_mutex, FuriWaitForever);
    m = app->model;
    furi_mutex_release(app->model_mutex);

    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil sensor (USART)");
    canvas_set_font(canvas, FontSecondary);

    if(m.state == LinkStateBusy) {
        canvas_draw_str(canvas, 2, 28, "USART busy (in use by");
        canvas_draw_str(canvas, 2, 38, "another service)");
    } else if(!m.have_reading) {
        canvas_draw_str(canvas, 2, 28, "Waiting for data...");
    } else {
        int t = m.reading.temperature_dc;
        int ta = t < 0 ? -t : t;
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", m.reading.moisture / 10, m.reading.moisture % 10);
        canvas_draw_str(canvas, 2, 26, line);
        snprintf(line, sizeof(line), "Temp: %s%d.%d C", t < 0 ? "-" : "", ta / 10, ta % 10);
        canvas_draw_str(canvas, 2, 38, line);
    }
    snprintf(
        line, sizeof(line), "OK:%lu Err:%lu Ovf:%lu", m.frames_ok, m.errors, m.rx_overflow);
    canvas_draw_str(canvas, 2, 52, line);
    canvas_draw_str(canvas, 2, 62, "Back: stop & exit");
}

static void input_cb(InputEvent* event, void* ctx) {
    SoilApp* app = ctx;
    // Event flags are not lossy like a queue: Back can never be dropped.
    if(event->key == InputKeyBack) furi_event_flag_set(app->events, EVT_BACK);
}

// ---- Serial RX: ISR context. Zero-timeout stream send only; no heap/log/block ----

static void serial_rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            uint8_t b = furi_hal_serial_async_rx(handle);
            if(furi_stream_buffer_send(app->rx_stream, &b, 1, 0) != 1) app->isr_overflow++;
        }
    }
    // Line errors (noise/overrun) are not requested (report_errors = false); the CRC
    // framing layer detects the resulting corruption.
}

// ---- Worker thread: drain stream, frame, publish ----

static int32_t worker_fn(void* ctx) {
    SoilApp* app = ctx;
    uint8_t chunk[64];
    SoilReading r;

    for(;;) {
        size_t n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), WORKER_POLL_MS);
        bool changed = false;
        bool got = false;
        for(size_t i = 0; i < n; i++) {
            if(soil_parser_feed(&app->parser, chunk[i], &r)) got = true; // keep latest of a burst
        }
        if(n > 0) changed = true;

        if(changed) {
            furi_mutex_acquire(app->model_mutex, FuriWaitForever);
            if(got) {
                app->model.reading = r;
                app->model.have_reading = true;
            }
            app->model.frames_ok = app->parser.frames_ok;
            app->model.errors = app->parser.errors;
            app->model.rx_overflow = app->isr_overflow;
            furi_mutex_release(app->model_mutex);
            view_port_update(app->view_port);
        }

        // Stop only once the stream is drained so a pending burst is not lost on exit
        // (the producer is already stopped when the flag is set).
        if(furi_thread_flags_get() & WORKER_STOP_FLAG) {
            if(furi_stream_buffer_is_empty(app->rx_stream)) break;
        }
    }
    return 0;
}

// ---- Entry ----

int32_t soil_link_app(void* p) {
    UNUSED(p);
    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(*app));
    soil_parser_reset(&app->parser);
    app->model_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->events = furi_event_flag_alloc();
    app->model.state = LinkStateStarting;

    // Acquire the serial interface first; NULL means another owner has it.
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
        app->worker = furi_thread_alloc_ex("SoilRx", 1024, worker_fn, app);
        furi_thread_start(app->worker);
        furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 default
        furi_hal_serial_async_rx_start(app->serial, serial_rx_cb, app, false);
        app->model.state = LinkStateRunning;
    } else {
        FURI_LOG_W(TAG, "USART busy");
        app->model.state = LinkStateBusy;
    }

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    furi_event_flag_wait(app->events, EVT_BACK, FuriFlagWaitAny, FuriWaitForever);

    // Teardown: detach GUI (no more input callbacks) -> stop producer -> drain/join worker
    // -> release serial -> free memory.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial); // ISR can no longer touch the stream
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);

        furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP_FLAG);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        furi_stream_buffer_free(app->rx_stream);
    }

    furi_event_flag_free(app->events);
    furi_mutex_free(app->model_mutex);
    free(app);
    return 0;
}
