#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <expansion/expansion.h>
#include <stdio.h>

#include "soil_frame.h"

#define TAG "SoilLink"

#define SOIL_BAUD          115200
#define RX_STREAM_SIZE     512 // ~44 ms of line rate at 115200 8N1
#define WORKER_POLL_MS     50
#define EXIT_FLAG          (1U << 0)
#define WORKER_STOP_FLAG   (1U << 0)

typedef enum {
    SoilStatusStarting,
    SoilStatusListening,
    SoilStatusBusy, // serial interface owned by someone else
} SoilStatus;

// Shared state, guarded by `mutex`.
typedef struct {
    SoilStatus status;
    bool has_reading;
    SoilReading reading;
    uint32_t frames;
    uint32_t errors; // parser errors + UART line errors
} SoilModel;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex;
    SoilModel model;

    FuriEventFlag* exit_flag; // Back; cannot be dropped like a queue message

    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    SoilParser parser; // worker thread only

    // Written only from the RX interrupt callback, read by the worker.
    volatile uint32_t isr_line_errors;
    volatile uint32_t isr_overflows;
} SoilApp;

/* ---- UI (GUI thread) ---- */

static void soil_draw_callback(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    SoilModel m;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    m = app->model;
    furi_mutex_release(app->mutex);

    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil Link  115200 8N1");
    canvas_set_font(canvas, FontSecondary);

    if(m.status == SoilStatusBusy) {
        canvas_draw_str(canvas, 2, 26, "USART busy (in use by");
        canvas_draw_str(canvas, 2, 38, "another service).");
    } else if(!m.has_reading) {
        canvas_draw_str(canvas, 2, 26, "Waiting for sensor...");
    } else {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", m.reading.moisture / 10, m.reading.moisture % 10);
        canvas_draw_str(canvas, 2, 26, line);
        int t = m.reading.temperature_dc;
        unsigned t_abs = (unsigned)(t < 0 ? -t : t);
        snprintf(line, sizeof(line), "Temp: %s%u.%u C", t < 0 ? "-" : "", t_abs / 10, t_abs % 10);
        canvas_draw_str(canvas, 2, 38, line);
    }
    snprintf(line, sizeof(line), "Frames: %lu", (unsigned long)m.frames);
    canvas_draw_str(canvas, 2, 52, line);
    snprintf(line, sizeof(line), "Errors: %lu", (unsigned long)m.errors);
    canvas_draw_str(canvas, 2, 62, line);
}

static void soil_input_callback(InputEvent* event, void* ctx) {
    SoilApp* app = ctx;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        furi_event_flag_set(app->exit_flag, EXIT_FLAG);
    }
}

/* ---- RX path ---- */

// Interrupt context: zero-timeout stream ops only, no heap, no logging.
static void soil_serial_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            uint8_t byte = furi_hal_serial_async_rx(handle);
            if(furi_stream_buffer_send(app->rx_stream, &byte, 1, 0) != 1) {
                app->isr_overflows++;
            }
        }
    }
    if(event & (FuriHalSerialRxEventFrameError | FuriHalSerialRxEventNoiseError |
                FuriHalSerialRxEventOverrunError)) {
        app->isr_line_errors++;
    }
}

static int32_t soil_worker(void* ctx) {
    SoilApp* app = ctx;
    uint8_t chunk[64];
    uint32_t seen_isr_errors = 0;

    for(;;) {
        if(furi_thread_flags_get() & WORKER_STOP_FLAG) break;

        size_t got = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), WORKER_POLL_MS);

        bool changed = false;
        SoilReading reading = {0};
        bool got_reading = false;
        for(size_t i = 0; i < got; i++) {
            if(soil_parser_feed(&app->parser, chunk[i], &reading)) {
                got_reading = true; // keep the newest of a burst
                changed = true;
            }
        }

        uint32_t isr_errors = app->isr_line_errors + app->isr_overflows;
        if(isr_errors != seen_isr_errors) {
            seen_isr_errors = isr_errors;
            changed = true;
        }

        if(changed) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(got_reading) {
                app->model.reading = reading;
                app->model.has_reading = true;
            }
            app->model.frames = app->parser.frame_count;
            app->model.errors = app->parser.error_count + seen_isr_errors;
            furi_mutex_release(app->mutex);
            // One redraw per received chunk, not per byte.
            view_port_update(app->view_port);
        }
    }
    return 0;
}

/* ---- Lifecycle ---- */

int32_t soil_link_app(void* p) {
    UNUSED(p);

    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(SoilApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->exit_flag = furi_event_flag_alloc();
    app->model.status = SoilStatusStarting;
    soil_parser_reset(&app->parser);

    // The expansion service may own the USART; pause it for the app's lifetime
    // and re-enable on exit.
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, soil_draw_callback, app);
    view_port_input_callback_set(app->view_port, soil_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    bool serial_started = false;
    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial == NULL) {
        FURI_LOG_E(TAG, "USART busy");
        app->model.status = SoilStatusBusy;
    } else {
        app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
        app->worker = furi_thread_alloc_ex("SoilLinkRx", 1024, soil_worker, app);
        furi_thread_start(app->worker);

        furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 default
        furi_hal_serial_async_rx_start(app->serial, soil_serial_rx_callback, app, true);
        serial_started = true;
        app->model.status = SoilStatusListening;
    }
    view_port_update(app->view_port);

    // Back is the only exit; block until it is pressed.
    furi_event_flag_wait(app->exit_flag, EXIT_FLAG, FuriFlagWaitAny, FuriWaitForever);

    // Teardown: producer first, then consumer, then the buffer they share.
    if(serial_started) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
    }
    if(app->serial) furi_hal_serial_control_release(app->serial);
    if(app->worker) {
        furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP_FLAG);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    if(app->rx_stream) furi_stream_buffer_free(app->rx_stream);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    furi_event_flag_free(app->exit_flag);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
