#include "tally.h"

#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex; // guards count; held only for short reads/writes
    FuriEventFlag* exit_flag; // set by input callback; cannot be dropped like a queue item
    uint32_t count;
} TallyApp;

#define TALLY_FLAG_EXIT (1U << 0)

// GUI thread. Short snapshot of the model, then draw.
static void tally_draw_callback(Canvas* canvas, void* context) {
    TallyApp* app = context;

    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    uint32_t count = app->count;
    furi_mutex_release(app->mutex);

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 2, AlignCenter, AlignTop, "Tally");
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);
}

// GUI thread. Short work only: update count under mutex or raise the exit flag.
static void tally_input_callback(InputEvent* event, void* context) {
    TallyApp* app = context;

    if(event->key == InputKeyBack) {
        if(event->type == InputTypeShort) {
            furi_event_flag_set(app->exit_flag, TALLY_FLAG_EXIT);
        }
        return;
    }

    bool changed = false;
    furi_check(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk);
    if(event->key == InputKeyUp && event->type == InputTypeShort) {
        app->count = tally_inc(app->count);
        changed = true;
    } else if(event->key == InputKeyDown && event->type == InputTypeShort) {
        app->count = tally_dec(app->count);
        changed = true;
    } else if(event->key == InputKeyOk && event->type == InputTypeLong) {
        app->count = 0;
        changed = true;
    }
    furi_mutex_release(app->mutex);

    if(changed) view_port_update(app->view_port);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);

    TallyApp* app = malloc(sizeof(TallyApp));
    app->count = 0;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->exit_flag = furi_event_flag_alloc();
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tally_draw_callback, app);
    view_port_input_callback_set(app->view_port, tally_input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    furi_event_flag_wait(
        app->exit_flag, TALLY_FLAG_EXIT, FuriFlagWaitAny | FuriFlagNoClear, FuriWaitForever);

    // Detach the ViewPort first so no draw/input callback can run past this point.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_event_flag_free(app->exit_flag);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
