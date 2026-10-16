#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <stdio.h>

#define COUNT_MAX 999999U
#define FLAG_EXIT (1U << 0)

typedef struct {
    FuriMutex* mutex; // guards count
    FuriEventFlag* exit_flag;
    ViewPort* view_port;
    uint32_t count;
} TallyApp;

static void tally_draw_callback(Canvas* canvas, void* ctx) {
    TallyApp* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    uint32_t count = app->count;
    furi_mutex_release(app->mutex);

    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)count);

    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontBigNumbers);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, buf);
}

// GUI thread: short, no blocking beyond the count mutex.
static void tally_input_callback(InputEvent* event, void* ctx) {
    TallyApp* app = ctx;

    if(event->key == InputKeyBack) {
        if(event->type == InputTypeShort) {
            furi_event_flag_set(app->exit_flag, FLAG_EXIT); // cannot be dropped
        }
        return;
    }

    bool changed = false;
    bool step = event->type == InputTypeShort || event->type == InputTypeRepeat;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(event->key == InputKeyUp && step) {
        if(app->count < COUNT_MAX) {
            app->count++;
            changed = true;
        }
    } else if(event->key == InputKeyDown && step) {
        if(app->count > 0) {
            app->count--;
            changed = true;
        }
    } else if(event->key == InputKeyOk && event->type == InputTypeLong) {
        if(app->count != 0) {
            app->count = 0;
            changed = true;
        }
    }
    furi_mutex_release(app->mutex);

    if(changed) view_port_update(app->view_port);
}

int32_t tally_counter_app(void* p) {
    UNUSED(p);

    TallyApp app = {0};
    app.mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app.exit_flag = furi_event_flag_alloc();
    app.view_port = view_port_alloc();
    view_port_draw_callback_set(app.view_port, tally_draw_callback, &app);
    view_port_input_callback_set(app.view_port, tally_input_callback, &app);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, app.view_port, GuiLayerFullscreen);

    furi_event_flag_wait(app.exit_flag, FLAG_EXIT, FuriFlagWaitAny, FuriWaitForever);

    // Detach from GUI first so no callback can run, then free.
    gui_remove_view_port(gui, app.view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(app.view_port);
    furi_event_flag_free(app.exit_flag);
    furi_mutex_free(app.mutex);
    return 0;
}
