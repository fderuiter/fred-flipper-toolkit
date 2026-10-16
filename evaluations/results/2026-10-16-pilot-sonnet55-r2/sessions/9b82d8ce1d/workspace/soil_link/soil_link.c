#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <expansion/expansion.h>
#include <stdio.h>

#include "soil_parser.h"

#define TAG "SoilLink"

#define SOIL_BAUD       115200
#define RX_STREAM_SIZE  512 // ~44 ms of line-rate data; worker also wakes on every byte
#define WORKER_POLL_MS  50

enum {
    FlagRedraw = 1U << 0,
    FlagExit = 1U << 1,
};

typedef enum {
    LinkOk,
    LinkBusy, // serial interface owned by something else
} LinkStatus;

typedef struct {
    // Protected by mutex (written by worker, read by draw)
    bool have_reading;
    SoilReading reading;
    uint32_t error_count; // rejected frames / bad length / bad CRC
    LinkStatus status;
} SoilModel;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex;
    SoilModel model;
    FuriEventFlag* flags;

    Expansion* expansion;
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    FuriThread* worker;
    volatile bool worker_stop; // paired with event flag wake-up; see worker_run
    uint32_t rx_overflow; // IRQ-written, accessed with __atomic builtins
} SoilApp;

// ---- IRQ context: zero-timeout stream ops only, no heap, no logging ----
static void soil_rx_callback(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(!(event & FuriHalSerialRxEventData)) return;
    while(furi_hal_serial_async_rx_available(handle)) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(app->rx_stream, &byte, 1, 0) != 1) {
            __atomic_add_fetch(&app->rx_overflow, 1, __ATOMIC_RELAXED);
        }
    }
}

// ---- Worker thread: framing and model update ----
static int32_t soil_worker(void* ctx) {
    SoilApp* app = ctx;
    SoilParser parser;
    soil_parser_reset(&parser);
    uint8_t chunk[32];
    uint32_t last_overflow = 0;

    while(!app->worker_stop) {
        size_t n = furi_stream_buffer_receive(app->rx_stream, chunk, sizeof(chunk), WORKER_POLL_MS);
        bool changed = false;

        uint32_t of = __atomic_load_n(&app->rx_overflow, __ATOMIC_RELAXED);
        uint32_t new_errors = of - last_overflow; // lost bytes corrupt framing
        last_overflow = of;
        if(new_errors) {
            soil_parser_reset(&parser);
            changed = true;
        }

        SoilReading latest;
        bool got = false;
        uint32_t errs = new_errors;
        for(size_t i = 0; i < n; i++) {
            SoilReading r;
            SoilParseResult res = soil_parser_feed(&parser, chunk[i], &r);
            if(res == SoilParseFrame) {
                latest = r; // burst: keep only the newest, draw once
                got = true;
            } else if(res == SoilParseError) {
                errs++;
            }
        }

        if(got || errs || changed) {
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            if(got) {
                app->model.reading = latest;
                app->model.have_reading = true;
            }
            app->model.error_count += errs;
            furi_mutex_release(app->mutex);
            furi_event_flag_set(app->flags, FlagRedraw);
        }
    }
    return 0;
}

// ---- GUI context: snapshot + draw only ----
static void soil_draw(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    SoilModel m;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    m = app->model;
    furi_mutex_release(app->mutex);

    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil sensor");
    canvas_set_font(canvas, FontSecondary);

    if(m.status == LinkBusy) {
        canvas_draw_str(canvas, 2, 30, "USART busy (in use)");
        canvas_draw_str(canvas, 2, 42, "Disable CLI/log on header");
    } else if(!m.have_reading) {
        canvas_draw_str(canvas, 2, 30, "Waiting for data...");
    } else {
        snprintf(line, sizeof(line), "Moisture: %u.%u %%", m.reading.moisture / 10, m.reading.moisture % 10);
        canvas_draw_str(canvas, 2, 26, line);
        int t = m.reading.temperature_dc;
        snprintf(line, sizeof(line), "Temp: %s%d.%d C", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
        canvas_draw_str(canvas, 2, 38, line);
    }
    snprintf(line, sizeof(line), "Errors: %lu", (unsigned long)m.error_count);
    canvas_draw_str(canvas, 2, 54, line);
}

// ---- GUI input context: never blocks, Back cannot be dropped (event flag) ----
static void soil_input(InputEvent* event, void* ctx) {
    SoilApp* app = ctx;
    if(event->type == InputTypeShort && event->key == InputKeyBack) {
        furi_event_flag_set(app->flags, FlagExit);
    }
}

int32_t soil_link_app(void* p) {
    UNUSED(p);
    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(SoilApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->flags = furi_event_flag_alloc();

    // Expansion protocol may own the USART header; pause it for this app only.
    app->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(app->expansion);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
        furi_hal_serial_init(app->serial, SOIL_BAUD); // 8N1 is the default frame format
        furi_hal_serial_async_rx_start(app->serial, soil_rx_callback, app, false);

        app->worker = furi_thread_alloc_ex("SoilWorker", 1024, soil_worker, app);
        furi_thread_start(app->worker);
    } else {
        app->model.status = LinkBusy;
        FURI_LOG_E(TAG, "USART busy");
    }

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, soil_draw, app);
    view_port_input_callback_set(app->view_port, soil_input, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    // Main loop: wait for redraw/exit with a bounded timeout.
    for(;;) {
        uint32_t f = furi_event_flag_wait(
            app->flags, FlagRedraw | FlagExit, FuriFlagWaitAny, FuriWaitForever);
        if(f & FuriFlagError) continue;
        if(f & FlagExit) break;
        view_port_update(app->view_port);
    }

    // Teardown: detach GUI -> stop producer (IRQ) -> stop+join worker -> release serial -> free.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        app->worker_stop = true; // worker polls every WORKER_POLL_MS, so it exits promptly
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
        furi_stream_buffer_free(app->rx_stream);
    }

    expansion_enable(app->expansion);
    furi_record_close(RECORD_EXPANSION);

    furi_event_flag_free(app->flags);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
