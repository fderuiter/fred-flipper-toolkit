// Reviewer-side reference solution for FRED task 04. MIT licensed, original.
// Loads /data/remote.ir (the app's data folder), parses it with the bounded
// parser and shows a summary. Storage I/O happens in the app thread before the
// UI starts; the draw callback only reads the immutable result.
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <stdio.h>
#include "ir_parser.h"

#define IR_FILE      APP_DATA_PATH("remote.ir")
#define FAP_MAX_FILE (16u * 1024u) /* tighter than the parser limit to bound RAM */

typedef struct {
    IrRemote* remote;
    IrResult result;
    bool file_ok;
    FuriMessageQueue* input_queue;
} Viewer;

static bool load(Viewer* v) {
    bool ok = false;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    char* buf = malloc(FAP_MAX_FILE);
    if(storage_file_open(file, IR_FILE, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(file);
        if(size <= FAP_MAX_FILE) {
            size_t got = storage_file_read(file, buf, (size_t)size);
            if(got == (size_t)size) {
                v->result = ir_parse(buf, got, v->remote);
                ok = true;
            }
        } else {
            v->result = (IrResult){IrErrTooLarge, 0};
            ok = true;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    free(buf);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static void draw(Canvas* canvas, void* context) {
    Viewer* v = context;
    char line[40];
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    if(!v->file_ok) {
        canvas_draw_str(canvas, 2, 12, "No readable remote.ir");
    } else if(v->result.code != IrOk) {
        snprintf(line, sizeof(line), "Rejected: %s", ir_error_name(v->result.code));
        canvas_draw_str(canvas, 2, 12, line);
        snprintf(line, sizeof(line), "line %lu", (unsigned long)v->result.line);
        canvas_draw_str(canvas, 2, 26, line);
    } else {
        snprintf(line, sizeof(line), "%lu signals", (unsigned long)v->remote->signal_count);
        canvas_draw_str(canvas, 2, 12, line);
        canvas_set_font(canvas, FontSecondary);
        for(uint32_t i = 0; i < v->remote->signal_count && i < 4; i++) {
            const IrSignal* s = &v->remote->signals[i];
            if(s->type == IrSignalParsed)
                snprintf(line, sizeof(line), "%.16s %.10s", s->name, s->protocol);
            else
                snprintf(
                    line, sizeof(line), "%.16s raw %lu", s->name, (unsigned long)s->timing_count);
            canvas_draw_str(canvas, 2, 26 + (int32_t)i * 10, line);
        }
    }
}

static void input(InputEvent* event, void* context) {
    Viewer* v = context;
    furi_message_queue_put(v->input_queue, event, 0);
}

int32_t fred_ir_viewer_app(void* p) {
    UNUSED(p);
    Viewer v = {0};
    v.remote = malloc(sizeof(IrRemote));
    v.input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    v.file_ok = load(&v);

    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw, &v);
    view_port_input_callback_set(vp, input, &v);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    InputEvent event;
    while(furi_message_queue_get(v.input_queue, &event, FuriWaitForever) == FuriStatusOk) {
        if(event.key == InputKeyBack && event.type == InputTypeShort) break;
    }

    view_port_enabled_set(vp, false);
    gui_remove_view_port(gui, vp);
    view_port_free(vp);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(v.input_queue);
    free(v.remote);
    return 0;
}
