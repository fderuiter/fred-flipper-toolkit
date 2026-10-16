/*
 * Parser for Flipper ".ir" remote files (Filetype "IR signals file", Version 1).
 *
 * Pure C99, no Flipper SDK dependencies, no heap allocation, no floats: the
 * same source builds in a FAP (API 88.2, f7, firmware 1.5.1) and on a PC.
 *
 * Streaming design: bytes are pushed in with ir_parser_feed() in chunks of any
 * size (e.g. 256 bytes from storage_file_read()). Lines are never buffered
 * whole, so line length is unbounded in the input but memory use is constant.
 * Raw timings are validated and counted but NOT stored.
 *
 *   static IrFile file;          // ~11 KB; keep off the thread stack
 *   IrParser p;
 *   ir_parser_init(&p, &file);
 *   while ((n = storage_file_read(f, buf, sizeof buf)) > 0)
 *       if (ir_parser_feed(&p, buf, n) != IR_OK) break;
 *   IrStatus st = ir_parser_finish(&p);   // IR_OK => file.signals[0..count)
 *
 * Parsing is all-or-nothing: on any error the file is rejected (file.count may
 * hold the signals completed so far, but callers should treat them as invalid).
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command,
 * frequency, duty_cycle, data. Any other key is rejected (IR_ERR_UNKNOWN_KEY).
 * Names are 1..IR_NAME_MAX printable ASCII chars; they are untrusted text and
 * must only ever be displayed, never interpreted.
 */
#ifndef IR_PARSER_H
#define IR_PARSER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef IR_MAX_SIGNALS
#define IR_MAX_SIGNALS 128 /* max buttons per file */
#endif
#ifndef IR_MAX_TIMINGS
#define IR_MAX_TIMINGS 1024 /* max raw timings per signal (firmware limit) */
#endif
#define IR_NAME_MAX 63
#define IR_PROTOCOL_MAX 15
#define IR_FREQ_MIN 10000u
#define IR_FREQ_MAX 56000u

typedef enum {
    IR_OK = 0,
    IR_ERR_EMPTY, /* no input at all */
    IR_ERR_HEADER, /* missing/wrong Filetype or malformed header */
    IR_ERR_VERSION, /* Version other than 1 */
    IR_ERR_SYNTAX, /* line is not "key: value", key before name, ... */
    IR_ERR_UNKNOWN_KEY, /* key outside the supported subset or wrong for type */
    IR_ERR_DUPLICATE_KEY,
    IR_ERR_MISSING_KEY, /* signal lacks a required key */
    IR_ERR_BAD_VALUE, /* malformed value (bad hex, non-digit, empty, ...) */
    IR_ERR_RANGE, /* number overflows or is outside the allowed range */
    IR_ERR_TOO_LONG, /* value longer than the fixed buffers */
    IR_ERR_BAD_CHAR, /* non-printable / non-ASCII byte */
    IR_ERR_TOO_MANY_TIMINGS,
    IR_ERR_TOO_MANY_SIGNALS,
    IR_ERR_TRUNCATED, /* input ended in the middle of a line */
} IrStatus;

typedef enum {
    IR_TYPE_PARSED = 0,
    IR_TYPE_RAW = 1,
} IrType;

typedef struct {
    char name[IR_NAME_MAX + 1];
    IrType type;
    char protocol[IR_PROTOCOL_MAX + 1]; /* parsed only */
    uint8_t address[4]; /* parsed only */
    uint8_t command[4]; /* parsed only */
    uint32_t frequency; /* raw only, Hz */
    uint32_t duty_cycle_ppm; /* raw only, 330000 == 0.33 */
    uint32_t timing_count; /* raw only */
} IrSignal;

typedef struct {
    IrSignal signals[IR_MAX_SIGNALS];
    size_t count;
} IrFile;

/* Internal parser state; treat as opaque. */
typedef struct {
    IrFile* out;
    IrStatus status;
    uint32_t line; /* 1-based line being read */
    uint32_t err_line; /* line of first error, 0 if none */
    uint8_t stage; /* 0: Filetype, 1: Version, 2: body */
    uint8_t state;
    uint8_t cr_pending;
    uint8_t any_input;
    uint8_t cur_open;
    uint8_t key_id;
    uint16_t seen; /* bitmask of keys seen in current signal */
    uint8_t key_len;
    uint8_t val_len;
    uint8_t tok_digits;
    uint32_t tok_val;
    char key[16];
    char val[IR_NAME_MAX + 1];
} IrParser;

void ir_parser_init(IrParser* p, IrFile* out);
/* Returns IR_OK or the (sticky) first error. */
IrStatus ir_parser_feed(IrParser* p, const char* data, size_t len);
/* Call once after the last chunk. */
IrStatus ir_parser_finish(IrParser* p);
/* Convenience: whole buffer at once. err_line may be NULL. */
IrStatus ir_parse_buffer(const char* data, size_t len, IrFile* out, uint32_t* err_line);

const char* ir_status_str(IrStatus st);
/* Line number (1-based) of the first error, 0 if none. */
uint32_t ir_parser_error_line(const IrParser* p);
/* One-line summary for the UI, always NUL-terminated, e.g.
 * "parsed NEC" or "raw 38000Hz 11 timings". Returns buf. */
char* ir_signal_describe(const IrSignal* s, char* buf, size_t buf_size);

#ifdef __cplusplus
}
#endif

#endif
