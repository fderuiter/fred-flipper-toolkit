// Reviewer-side reference solution for FRED task 01. MIT licensed, original.
// One custom screen: ViewPort + Canvas, input handed to the app thread via a queue.
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

typedef struct {
    FuriMessageQueue* input_queue;
    FuriMutex* mutex;
    uint32_t count; /* guarded by mutex */
} Tally;

static void tally_draw(Canvas* canvas, void* context) {
    Tally* tally = context;
    char text[12];
    furi_check(furi_mutex_acquire(tally->mutex, FuriWaitForever) == FuriStatusOk);
    snprintf(text, sizeof(text), "%lu", (unsigned long)tally->count);
    furi_mutex_release(tally->mutex);
    canvas_clear(canvas);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, text);
}

static void tally_input(InputEvent* event, void* context) {
    Tally* tally = context;
    /* GUI thread: never block. If the queue is full the event is dropped; the
     * queue is sized so that Back cannot be starved by ordinary key bursts. */
    furi_message_queue_put(tally->input_queue, event, 0);
}

int32_t fred_tally_app(void* p) {
    UNUSED(p);
    Tally tally = {.count = 0};
    tally.input_queue = furi_message_queue_alloc(16, sizeof(InputEvent));
    tally.mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, tally_draw, &tally);
    view_port_input_callback_set(view_port, tally_input, &tally);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(tally.input_queue, &event, FuriWaitForever) != FuriStatusOk)
            continue;
        bool changed = false;
        furi_check(furi_mutex_acquire(tally.mutex, FuriWaitForever) == FuriStatusOk);
        if(event.key == InputKeyBack && event.type == InputTypeShort) {
            running = false;
        } else if(
            event.key == InputKeyUp &&
            (event.type == InputTypeShort || event.type == InputTypeRepeat)) {
            if(tally.count < UINT32_MAX) tally.count++;
            changed = true;
        } else if(
            event.key == InputKeyDown &&
            (event.type == InputTypeShort || event.type == InputTypeRepeat)) {
            if(tally.count > 0) tally.count--;
            changed = true;
        } else if(event.key == InputKeyOk && event.type == InputTypeLong) {
            tally.count = 0;
            changed = true;
        }
        furi_mutex_release(tally.mutex);
        if(changed) view_port_update(view_port);
    }

    /* Detach from GUI first so no draw/input callback can run, then free. */
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(tally.input_queue);
    furi_mutex_free(tally.mutex);
    return 0;
}
