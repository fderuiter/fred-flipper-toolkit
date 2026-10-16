#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#include "tally_logic.h"

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    uint32_t count; // guarded by mutex
} TallyApp;

static void tally_draw_callback(Canvas* canvas, void* ctx) {
    TallyApp* app = ctx;
    uint32_t count = 0;
    if(furi_mutex_acquire(app->mutex, 25) != FuriStatusOk) return;
    count = app->count;
    furi_mutex_release(app->mutex);

    char text[12];
    snprintf(text, sizeof(text), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, text);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Hold OK: reset");
}

// GUI thread: only forward the event to the app thread.
static void tally_input_callback(InputEvent* event, void* ctx) {
    TallyApp* app = ctx;
    // If the queue is full a count press is dropped; 8 slots is ample for human input.
    furi_message_queue_put(app->queue, event, 0);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);
    TallyApp* app = malloc(sizeof(TallyApp));
    app->count = 0;
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, tally_draw_callback, app);
    view_port_input_callback_set(app->view_port, tally_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) != FuriStatusOk) continue;

        if(event.key == InputKeyBack) {
            if(event.type == InputTypeShort) running = false;
            continue;
        }

        bool changed = false;
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event.type == InputTypeShort && event.key == InputKeyUp) {
            app->count = tally_increment(app->count);
            changed = true;
        } else if(event.type == InputTypeShort && event.key == InputKeyDown) {
            app->count = tally_decrement(app->count);
            changed = true;
        } else if(event.type == InputTypeLong && event.key == InputKeyOk) {
            app->count = 0;
            changed = true;
        }
        furi_mutex_release(app->mutex);
        if(changed) view_port_update(app->view_port);
    }

    // Detach from GUI first so no callback can run during teardown.
    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
