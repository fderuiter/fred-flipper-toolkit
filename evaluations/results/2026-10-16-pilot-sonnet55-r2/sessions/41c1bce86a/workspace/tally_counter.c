#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#define COUNT_MAX 999999

typedef struct {
    FuriMutex* mutex;
    uint32_t count;
} TallyState;

static void render_callback(Canvas* canvas, void* ctx) {
    TallyState* state = ctx;
    furi_mutex_acquire(state->mutex, FuriWaitForever);

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)state->count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);

    furi_mutex_release(state->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);

    TallyState state = {.mutex = furi_mutex_alloc(FuriMutexTypeNormal), .count = 0};
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, &state);
    view_port_input_callback_set(view_port, input_callback, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(queue, &event, FuriWaitForever) != FuriStatusOk) continue;

        furi_mutex_acquire(state.mutex, FuriWaitForever);
        if(event.type == InputTypeShort) {
            switch(event.key) {
            case InputKeyUp:
                if(state.count < COUNT_MAX) state.count++;
                break;
            case InputKeyDown:
                if(state.count > 0) state.count--;
                break;
            case InputKeyBack:
                running = false;
                break;
            default:
                break;
            }
        } else if(event.type == InputTypeLong && event.key == InputKeyOk) {
            state.count = 0;
        }
        furi_mutex_release(state.mutex);
        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    furi_mutex_free(state.mutex);
    return 0;
}
