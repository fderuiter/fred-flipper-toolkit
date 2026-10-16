#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#include "tally_logic.h"

#define TAG "TallyCounter"

typedef struct {
    Gui* gui; // borrowed record
    ViewPort* view_port; // owned
    FuriMutex* mutex; // guards count
    FuriEventFlag* exit_flag; // set by Back, awaited by main thread
    uint32_t count;
} TallyApp;

#define TALLY_EVENT_EXIT (1U << 0)

// GUI thread. Take a snapshot under the mutex, draw outside it.
static void tally_draw_callback(Canvas* canvas, void* ctx) {
    TallyApp* app = ctx;
    uint32_t count = 0;
    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
        count = app->count;
        furi_mutex_release(app->mutex);
    }

    char text[16];
    snprintf(text, sizeof(text), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 30, AlignCenter, AlignCenter, text);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Hold OK: reset");
}

// GUI thread. Short, no blocking beyond the brief mutex.
static void tally_input_callback(InputEvent* event, void* ctx) {
    TallyApp* app = ctx;

    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        // A flag cannot be dropped the way a full queue message can.
        furi_event_flag_set(app->exit_flag, TALLY_EVENT_EXIT);
        return;
    }

    bool changed = false;
    if(furi_mutex_acquire(app->mutex, FuriWaitForever) == FuriStatusOk) {
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
    }
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

    furi_event_flag_wait(app->exit_flag, TALLY_EVENT_EXIT, FuriFlagWaitAny, FuriWaitForever);

    // Detach from the GUI first so no callback can run while freeing.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_event_flag_free(app->exit_flag);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
