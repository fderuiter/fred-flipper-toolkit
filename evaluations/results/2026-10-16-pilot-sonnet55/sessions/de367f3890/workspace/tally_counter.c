#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#include "tally_logic.h"

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMutex* mutex; // guards count; held only for short reads/writes
    FuriMessageQueue* exit_queue; // capacity 1: a pending exit request is never lost
    uint32_t count;
} TallyApp;

// Context: GUI thread. Snapshot the count, then draw.
static void tally_draw_callback(Canvas* canvas, void* ctx) {
    TallyApp* app = ctx;

    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint32_t count = app->count;
    furi_mutex_release(app->mutex);

    char text[16];
    snprintf(text, sizeof(text), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, text);
}

// Context: GUI thread. Counter updates are tiny, so they are applied here.
static void tally_input_callback(InputEvent* event, void* ctx) {
    TallyApp* app = ctx;

    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyUp:
        case InputKeyDown:
            furi_mutex_acquire(app->mutex, FuriWaitForever);
            app->count = (event->key == InputKeyUp) ? tally_increment(app->count) :
                                                      tally_decrement(app->count);
            furi_mutex_release(app->mutex);
            view_port_update(app->view_port);
            break;
        case InputKeyBack: {
            uint8_t exit_msg = 1;
            // Full queue means an exit is already pending; nothing is lost.
            furi_message_queue_put(app->exit_queue, &exit_msg, 0);
            break;
        }
        default:
            break;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        app->count = tally_reset();
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);

    TallyApp* app = malloc(sizeof(TallyApp));
    app->count = 0;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->exit_queue = furi_message_queue_alloc(1, sizeof(uint8_t));
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tally_draw_callback, app);
    view_port_input_callback_set(app->view_port, tally_input_callback, app);

    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    uint8_t msg;
    furi_message_queue_get(app->exit_queue, &msg, FuriWaitForever);

    // Teardown: detach from GUI first so no callback can run, then free.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->exit_queue);
    furi_mutex_free(app->mutex);
    free(app);

    return 0;
}
