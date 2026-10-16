#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#define TALLY_MAX 999999U

typedef struct {
    uint32_t count;
} TallyState;

typedef struct {
    TallyState* state;
    FuriMutex* mutex;
} TallyApp;

static void tally_draw_callback(Canvas* canvas, void* ctx) {
    TallyApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint32_t count = app->state->count;
    furi_mutex_release(app->mutex);

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);
}

static void tally_input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, 0);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);

    TallyState state = {.count = 0};
    TallyApp app = {.state = &state, .mutex = furi_mutex_alloc(FuriMutexTypeNormal)};
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, tally_draw_callback, &app);
    view_port_input_callback_set(view_port, tally_input_callback, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(queue, &event, FuriWaitForever) != FuriStatusOk) continue;

        furi_mutex_acquire(app.mutex, FuriWaitForever);
        if(event.type == InputTypeShort || event.type == InputTypeRepeat) {
            if(event.key == InputKeyUp && state.count < TALLY_MAX) {
                state.count++;
            } else if(event.key == InputKeyDown && state.count > 0) {
                state.count--;
            }
        }
        if(event.type == InputTypeShort && event.key == InputKeyBack) {
            running = false;
        } else if(event.type == InputTypeLong && event.key == InputKeyOk) {
            state.count = 0;
        }
        furi_mutex_release(app.mutex);

        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    furi_mutex_free(app.mutex);
    return 0;
}
