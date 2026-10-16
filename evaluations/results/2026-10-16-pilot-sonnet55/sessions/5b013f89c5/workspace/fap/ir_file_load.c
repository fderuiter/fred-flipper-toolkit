/*
 * Storage adapter for ir_parser. NOT COMPILED: no Flipper SDK in this workspace, and the
 * storage_* symbols were not checked against api_symbols.csv for API 88.2.
 *
 * Call from an app/worker thread only (storage calls block on the storage service);
 * never from draw/input callbacks. IrRemote is ~6 KB: allocate it once with malloc
 * (fixed size, not input dependent) and free it on teardown; do not put it on the stack.
 *
 * Add ir_parser/ir_parser.c to the FAP `sources` in application.fam.
 */
#include <furi.h>
#include <storage/storage.h>

#include "../ir_parser/ir_parser.h"

static size_t ir_storage_read(void* ctx, uint8_t* buf, size_t cap) {
    File* file = ctx;
    size_t n = storage_file_read(file, buf, (uint16_t)(cap > UINT16_MAX ? UINT16_MAX : cap));
    /* storage_file_read returns 0 on both EOF and error; check the error state to tell apart. */
    if(n == 0 && storage_file_get_error(file) != FSE_OK) return IR_READ_ERROR;
    return n;
}

/* Returns IrOk and fills *out, or the parse/IO error. */
IrError ir_file_load(Storage* storage, const char* path, IrRemote* out) {
    File* file = storage_file_alloc(storage);
    IrError err;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        err = ir_parse_stream(ir_storage_read, file, out);
        storage_file_close(file);
    } else {
        memset(out, 0, sizeof(*out));
        out->error = err = IrErrIo;
    }
    storage_file_free(file);
    return err;
}

/* UI text for one list row: "Power  parsed NEC" / "Fan  raw 38000Hz 11 timings" */
void ir_signal_summary(const IrSignal* s, char* out, size_t out_size) {
    if(s->type == IrSignalParsed) {
        snprintf(out, out_size, "%s: parsed %s", s->name, s->protocol);
    } else {
        snprintf(
            out, out_size, "%s: raw %luHz %lu timings", s->name, (unsigned long)s->frequency,
            (unsigned long)s->timing_count);
    }
}
