// Reviewer-side reference solution for FRED held-out task H01. MIT licensed, original.
// One ViewPort. Time comes from furi_get_tick(); the 50 ms timer only asks for a
// redraw, so a slow redraw cannot make the clock drift.
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>
#include "stopwatch_core.h"

typedef struct {
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;
    ViewPort* view_port;
    Stopwatch sw; /* guarded by mutex */
} App;

static uint32_t now_ms(void) {
    /* 64-bit intermediate keeps the conversion exact for any tick rate. */
    return (uint32_t)((uint64_t)furi_get_tick() * 1000u / furi_kernel_get_tick_frequency());
}

static void app_draw(Canvas* canvas, void* context) {
    App* app = context;
    char main_time[12];
    char laps[3][16];
    uint32_t shown = 0;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    stopwatch_format(stopwatch_elapsed(&app->sw, now_ms()), main_time, sizeof(main_time));
    for(uint32_t i = 0; i < 3; i++) {
        uint32_t lap_ms;
        if(!stopwatch_lap_get(&app->sw, i, &lap_ms)) break;
        char t[12];
        stopwatch_format(lap_ms, t, sizeof(t));
        snprintf(laps[i], sizeof(laps[i]), "L%lu %s", (unsigned long)(app->sw.lap_count - i), t);
        shown++;
    }
    furi_mutex_release(app->mutex);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 14, AlignCenter, AlignCenter, main_time);
    canvas_set_font(canvas, FontSecondary);
    for(uint32_t i = 0; i < shown; i++) canvas_draw_str(canvas, 4, 36 + 10 * i, laps[i]);
}

static void app_input(InputEvent* event, void* context) {
    App* app = context;
    furi_message_queue_put(app->input_queue, event, 0); /* GUI thread: never block */
}

static void app_tick(void* context) {
    App* app = context;
    view_port_update(app->view_port); /* redraw request only */
}

int32_t fred_stopwatch_app(void* p) {
    UNUSED(p);
    App app = {0};
    stopwatch_reset(&app.sw);
    app.input_queue = furi_message_queue_alloc(16, sizeof(InputEvent));
    app.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.view_port = view_port_alloc();
    view_port_draw_callback_set(app.view_port, app_draw, &app);
    view_port_input_callback_set(app.view_port, app_input, &app);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app.view_port, GuiLayerFullscreen);
    FuriTimer* timer = furi_timer_alloc(app_tick, FuriTimerTypePeriodic, &app);
    furi_timer_start(timer, furi_ms_to_ticks(50));

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app.input_queue, &event, FuriWaitForever) != FuriStatusOk)
            continue;
        if(event.key == InputKeyBack && event.type == InputTypeShort) {
            running = false;
            continue;
        }
        furi_check(furi_mutex_acquire(app.mutex, FuriWaitForever) == FuriStatusOk);
        uint32_t now = now_ms();
        if(event.key == InputKeyOk && event.type == InputTypeShort) {
            stopwatch_toggle(&app.sw, now);
        } else if(event.key == InputKeyOk && event.type == InputTypeLong) {
            stopwatch_long_ok(&app.sw);
        } else if(event.key == InputKeyUp && event.type == InputTypeShort) {
            stopwatch_lap(&app.sw, now);
        }
        furi_mutex_release(app.mutex);
        view_port_update(app.view_port);
    }

    /* Timer first: furi_timer_free waits for the timer daemon (claim C-TMR-002),
     * so no tick callback can touch the ViewPort after this returns. */
    furi_timer_stop(timer);
    furi_timer_free(timer);
    view_port_enabled_set(app.view_port, false);
    gui_remove_view_port(gui, app.view_port);
    view_port_free(app.view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(app.input_queue);
    furi_mutex_free(app.mutex);
    return 0;
}
