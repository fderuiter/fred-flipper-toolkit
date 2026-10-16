#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

typedef struct {
    FuriMutex* mutex;
    uint32_t count;
} TallyApp;

static void draw_callback(Canvas* canvas, void* ctx) {
    TallyApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint32_t count = app->count;
    furi_mutex_release(app->mutex);

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);

    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 62, AlignCenter, AlignBottom, "Hold OK: reset");
}

static void input_callback(InputEvent* event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    furi_message_queue_put(queue, event, FuriWaitForever);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);

    TallyApp* app = malloc(sizeof(TallyApp));
    app->count = 0;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, draw_callback, app);
    view_port_input_callback_set(view_port, input_callback, queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    InputEvent event;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(queue, &event, FuriWaitForever) != FuriStatusOk) continue;

        bool step = event.type == InputTypeShort || event.type == InputTypeRepeat;

        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(event.key == InputKeyBack) {
            if(event.type == InputTypeShort) running = false;
        } else if(event.key == InputKeyUp) {
            if(step && app->count < 999999999UL) app->count++;
        } else if(event.key == InputKeyDown) {
            if(step && app->count > 0) app->count--;
        } else if(event.key == InputKeyOk) {
            if(event.type == InputTypeLong) app->count = 0;
        }
        furi_mutex_release(app->mutex);
        view_port_update(view_port);
    }

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
