/* Soil sensor link: shows the latest reading from a UART soil sensor board (115200 8N1) on the
 * USART header (pin 13 TX, pin 14 RX), plus counters.
 *
 * Electrical: this app only enables the USART RX/TX peripheral. It never drives GPIO pins as
 * outputs by itself and never enables the 5 V rail. Header pins are 3.3 V logic; the sensor
 * board's logic level and power are unconfirmed, so do not connect it until they are known.
 *
 * Threads/contexts:
 *   - ISR (serial RX callback): copies bytes into `rx_stream` with zero timeout, counts drops.
 *   - worker thread: drains `rx_stream`, runs the parser, updates the model under `mutex`.
 *   - GUI thread: draw callback (reads a snapshot under `mutex`), input callback (sets a flag).
 *   - main thread: owns setup/teardown, waits for the Back flag.
 */

#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <gui/gui.h>
#include <expansion/expansion.h>
#include <stdio.h>

#include "soil_proto.h"

#define TAG "SoilLink"

#define SOIL_BAUD          115200
#define RX_STREAM_SIZE     1024 /* ~89 ms of line rate; worker is woken per byte */
#define ISR_CHUNK          32
#define WORKER_STACK       2048
#define WORKER_POLL_MS     50
#define INTERBYTE_TIMEOUT  300 /* ms of silence that abandons a partial frame */
#define REDRAW_MIN_MS      100

#define EVT_BACK (1UL << 0) /* FuriEventFlag, main thread wakeup */
#define WORKER_STOP (1UL << 0) /* thread flag, worker */

typedef enum {
    LinkStateStarting,
    LinkStateRunning,
    LinkStateBusy, /* serial interface unavailable */
} LinkState;

typedef struct {
    LinkState link;
    bool have_reading;
    SoilReading reading;
    uint32_t frames_ok;
    uint32_t errors;
    uint32_t dropped_bytes; /* RX stream overflow */
} SoilModel;

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex; /* guards `model` */
    SoilModel model;
    FuriEventFlag* events;

    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx_stream;
    uint32_t isr_dropped; /* written only by the ISR, read with __atomic_load_n */
    FuriThread* worker;
} SoilApp;

/* ---- ISR ---------------------------------------------------------------------------------- */

static void soil_serial_isr(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    SoilApp* app = ctx;
    if(!(event & FuriHalSerialRxEventData)) return;

    uint8_t chunk[ISR_CHUNK];
    while(furi_hal_serial_async_rx_available(handle)) {
        size_t n = 0;
        while(n < ISR_CHUNK && furi_hal_serial_async_rx_available(handle)) {
            chunk[n++] = furi_hal_serial_async_rx(handle);
        }
        const size_t sent = furi_stream_buffer_send(app->rx_stream, chunk, n, 0);
        if(sent != n) {
            __atomic_fetch_add(&app->isr_dropped, (uint32_t)(n - sent), __ATOMIC_RELAXED);
        }
    }
}

/* ---- Worker ------------------------------------------------------------------------------- */

static void soil_publish(SoilApp* app, const SoilEvents* ev, uint32_t dropped) {
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(ev->has_reading) {
        app->model.have_reading = true;
        app->model.reading = ev->reading;
    }
    app->model.frames_ok += ev->frames_ok;
    app->model.errors += ev->errors;
    app->model.dropped_bytes = dropped;
    furi_mutex_release(app->mutex);
}

static int32_t soil_worker(void* ctx) {
    SoilApp* app = ctx;
    SoilParser parser;
    soil_parser_reset(&parser);

    uint8_t buf[64];
    uint32_t last_rx = furi_get_tick();
    uint32_t last_redraw = 0;
    bool dirty = false;

    for(;;) {
        const uint32_t flags = furi_thread_flags_wait(WORKER_STOP, FuriFlagWaitAny, 0);
        if(!(flags & FuriFlagError) && (flags & WORKER_STOP)) break;

        const size_t n = furi_stream_buffer_receive(app->rx_stream, buf, sizeof(buf), WORKER_POLL_MS);
        SoilEvents ev = {0};

        if(n > 0) {
            last_rx = furi_get_tick();
            for(size_t i = 0; i < n; i++) soil_parser_feed(&parser, buf[i], &ev);
        } else if(furi_get_tick() - last_rx > INTERBYTE_TIMEOUT) {
            soil_parser_timeout(&parser, &ev);
            last_rx = furi_get_tick();
        }

        const uint32_t dropped = __atomic_load_n(&app->isr_dropped, __ATOMIC_RELAXED);
        if(ev.has_reading || ev.errors || ev.frames_ok || dropped != app->model.dropped_bytes) {
            soil_publish(app, &ev, dropped);
            dirty = true;
        }

        /* Coalesce redraws: a burst of frames triggers at most one redraw per interval. */
        if(dirty && (furi_get_tick() - last_redraw) >= REDRAW_MIN_MS) {
            view_port_update(app->view_port);
            last_redraw = furi_get_tick();
            dirty = false;
        }
    }
    if(dirty) view_port_update(app->view_port);
    return 0;
}

/* ---- GUI ---------------------------------------------------------------------------------- */

static void soil_draw(Canvas* canvas, void* ctx) {
    SoilApp* app = ctx;
    SoilModel m;
    if(furi_mutex_acquire(app->mutex, 50) != FuriStatusOk) return;
    m = app->model;
    furi_mutex_release(app->mutex);

    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Soil sensor");
    canvas_set_font(canvas, FontSecondary);

    if(m.link == LinkStateBusy) {
        canvas_draw_str(canvas, 2, 28, "USART busy (in use by");
        canvas_draw_str(canvas, 2, 38, "another service).");
        canvas_draw_str(canvas, 2, 62, "Back: exit");
        return;
    }

    if(m.have_reading) {
        snprintf(line, sizeof(line), "Moisture: %u / 1000", (unsigned)m.reading.moisture);
        canvas_draw_str(canvas, 2, 24, line);
        const int t = m.reading.temperature_dc;
        const unsigned a = (unsigned)(t < 0 ? -t : t);
        snprintf(line, sizeof(line), "Temp: %s%u.%u C", t < 0 ? "-" : "", a / 10, a % 10);
        canvas_draw_str(canvas, 2, 35, line);
    } else {
        canvas_draw_str(canvas, 2, 24, "Waiting for data...");
    }

    snprintf(line, sizeof(line), "OK: %lu  Errors: %lu", m.frames_ok, m.errors);
    canvas_draw_str(canvas, 2, 48, line);
    snprintf(line, sizeof(line), "Dropped bytes: %lu", m.dropped_bytes);
    canvas_draw_str(canvas, 2, 58, line);
}

static void soil_input(InputEvent* event, void* ctx) {
    SoilApp* app = ctx;
    /* Back is signalled with an event flag, which cannot be lost to a full queue. */
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        furi_event_flag_set(app->events, EVT_BACK);
    }
}

/* ---- Entry point -------------------------------------------------------------------------- */

int32_t soil_link_app(void* p) {
    UNUSED(p);

    SoilApp* app = malloc(sizeof(SoilApp));
    memset(app, 0, sizeof(SoilApp));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->events = furi_event_flag_alloc();
    app->rx_stream = furi_stream_buffer_alloc(RX_STREAM_SIZE, 1);
    app->model.link = LinkStateStarting;

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, soil_draw, app);
    view_port_input_callback_set(app->view_port, soil_input, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    /* The expansion service may own the USART; disable it while we run and re-enable on exit
     * (we only restore what we changed). */
    Expansion* expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(expansion);

    app->serial = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(app->serial) {
        furi_hal_serial_init(app->serial, SOIL_BAUD); /* 8N1 is the default framing */
        furi_hal_serial_async_rx_start(app->serial, soil_serial_isr, app, false);

        app->worker = furi_thread_alloc_ex("SoilLinkRx", WORKER_STACK, soil_worker, app);
        furi_thread_start(app->worker);
        app->model.link = LinkStateRunning;
    } else {
        FURI_LOG_E(TAG, "USART busy");
        app->model.link = LinkStateBusy;
    }
    view_port_update(app->view_port);

    furi_event_flag_wait(app->events, EVT_BACK, FuriFlagWaitAny, FuriWaitForever);

    /* Teardown: stop the producer, then the consumer, then free what they shared. */
    if(app->serial) {
        furi_hal_serial_async_rx_stop(app->serial);
        furi_hal_serial_deinit(app->serial);
        furi_hal_serial_control_release(app->serial);
        app->serial = NULL;

        furi_thread_flags_set(furi_thread_get_id(app->worker), WORKER_STOP);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }

    expansion_enable(expansion);
    furi_record_close(RECORD_EXPANSION);

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);

    furi_stream_buffer_free(app->rx_stream);
    furi_event_flag_free(app->events);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
