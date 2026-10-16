#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <expansion/expansion.h>

#include <stdio.h>

#include "soil_frame.h"

#define TAG          "SoilLink"
#define SOIL_BAUD    115200
#define RX_STREAM_SZ 1024 // ~89 ms of line rate at 115200 8N1; worker drains continuously
#define WORKER_WAIT  50

#define FLAG_EXIT (1U << 0)

typedef enum {
    LinkStateRunning,
    LinkStateBusy, // serial handle unavailable
} LinkState;

typedef struct {
    FuriMutex* mutex; // guards the fields below
    SoilReading reading;
    bool has_reading;
    uint32_t errors;
    uint32_t frames;
    uint32_t rx_overflow;
    LinkState link;
} SoilModel;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    SoilModel model;

    FuriThreadId main_thread;
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    volatile bool worker_stop; // paired with a thread flag wake-up
    volatile uint32_t isr_overflow; // written only in ISR callback, read after RX stopped
} SoilApp;

// ---------------------------------------------------------------- UI (GUI thread)

static void draw_cb(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    SoilModel snap;
    furi_mutex_acquire(app->model.mutex, FuriWaitForever);
    snap = app->model;
    furi_mutex_release(app->model.mutex);

    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Soil sensor");
    canvas_set_font(canvas, FontSecondary);

    if(snap.link == LinkStateBusy) {
        canvas_draw_str(canvas, 2, 30, "USART busy (in use by");
        canvas_draw_str(canvas, 2, 42, "another service)");
    } else if(snap.has_reading) {
        int16_t t = snap.reading.temperature_dc;
        unsigned abs_t = (unsigned)(t < 0 ? -(int)t : t);
        snprintf(
            line, sizeof(line), "Moisture: %u.%u %%", snap.reading.moisture / 10, snap.reading.moisture % 10);
        canvas_draw_str(canvas, 2, 28, line);
        snprintf(line, sizeof(line), "Temp: %s%u.%u C", t < 0 ? "-" : "", abs_t / 10, abs_t % 10);
        canvas_draw_str(canvas, 2, 40, line);
    } else {
        canvas_draw_str(canvas, 2, 30, "Waiting for data...");
    }
    snprintf(line, sizeof(line), "Errors: %lu  OK: %lu", snap.errors, snap.frames);
    canvas_draw_str(canvas, 2, 54, line);
    if(snap.rx_overflow) {
        snprintf(line, sizeof(line), "Overflow: %lu", snap.rx_overflow);
        canvas_draw_str(canvas, 2, 63, line);
    }
}

static void input_cb(InputEvent* event, void* ctx) {
    SoilApp* app = ctx;
    // Thread flags cannot be lost to a full queue, so Back always reaches the main thread.
    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        furi_thread_flags_set(app->main_thread, FLAG_EXIT);
    }
}

// ---------------------------------------------------------------- RX (IRQ context)

static void rx_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    // IRQ context: no heap, no logging, zero-timeout stream ops only.
    if(event & FuriHalSerialRxEventData) {
        uint8_t b = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->rx_stream, &b, 1, 0) != 1) {
            app->isr_overflow++;
        }
    }
}

// ---------------------------------------------------------------- worker thread

static int32_t worker_fn(void* ctx) {
    SoilApp* app = ctx;
    SoilParser parser;
    soil_parser_reset(&parser);
    uint8_t chunk[64];

    while(!app->worker_stop) {
        size_t n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), WORKER_WAIT);
        bool changed = false;
        SoilReading latest;
        bool got = false;
        for(size_t i = 0; i < n; i++) {
            SoilReading r;
            if(soil_parser_feed(&parser, chunk[i], &r)) {
                latest = r; // burst: only the newest reading is kept
                got = true;
            }
        }
        if(n > 0 || app->isr_overflow) {
            furi_mutex_acquire(app->model.mutex, FuriWaitForever);
            if(got) {
                app->model.reading = latest;
                app->model.has_reading = true;
            }
            if(app->model.errors != parser.error_count || app->model.frames != parser.frame_count ||
               app->model.rx_overflow != app->isr_overflow) {
                changed = true;
            }
            app->model.errors = parser.error_count;
            app->model.frames = parser.frame_count;
            app->model.rx_overflow = app->isr_overflow;
            furi_mutex_release(app->model.mutex);
            if(changed || got) view_port_update(app->view_port);
        }
    }
    return 0;
}

// ---------------------------------------------------------------- entry

int32_t soil_link_app(void* p) {
    UNUSED(p);
    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(SoilApp));
    app->main_thread = furi_thread_get_current_id();
    app->model.mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    // Expansion service may own the USART header; pause it while we use the port.
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SZ, 1);
        furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 is the HAL default
        app->model.link = LinkStateRunning;

        app->worker = furi_thread_alloc_ex("SoilRx", 1024, worker_fn, app);
        furi_thread_start(app->worker);
        furi_hal_serial_async_rx_start(app->serial, rx_cb, app, false);
    } else {
        FURI_LOG_E(TAG, "USART busy");
        app->model.link = LinkStateBusy;
    }

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_cb, app);
    view_port_input_callback_set(app->view_port, input_cb, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    furi_thread_flags_wait(FLAG_EXIT, FuriFlagWaitAny, FuriWaitForever);

    // Teardown: detach input first, stop producer, then consumer, then free.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
        app->worker_stop = true;
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        furi_stream_buffer_free(app->rx_stream);
    }

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    furi_mutex_free(app->model.mutex);
    free(app);
    return 0;
}
