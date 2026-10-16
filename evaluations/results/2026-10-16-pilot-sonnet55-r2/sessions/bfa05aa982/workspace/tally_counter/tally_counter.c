#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

typedef struct {
    FuriMutex* mutex;
    uint32_t count;
} TallyState;

static void tally_draw_callback(Canvas* canvas, void* ctx) {
    TallyState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);
    uint32_t count = state->count;
    furi_mutex_release(state->mutex);

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "Tally Counter");
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, buf);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Hold OK: reset");
}

static void tally_input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);

    TallyState* state = malloc(sizeof(TallyState));
    state->count = 0;
    state->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, tally_draw_callback, state);
    view_port_input_callback_set(view_port, tally_input_callback, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent ev;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(queue, &ev, FuriWaitForever) != FuriStatusOk) continue;

        furi_mutex_acquire(state->mutex, FuriWaitForever);
        if(ev.type == InputTypeShort) {
            switch(ev.key) {
            case InputKeyUp:
                if(state->count < 999999) state->count++;
                break;
            case InputKeyDown:
                if(state->count > 0) state->count--;
                break;
            case InputKeyBack:
                running = false;
                break;
            default:
                break;
            }
        } else if(ev.type == InputTypeLong && ev.key == InputKeyOk) {
            state->count = 0;
        }
        furi_mutex_release(state->mutex);
        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    furi_mutex_free(state->mutex);
    free(state);
    return 0;
}
