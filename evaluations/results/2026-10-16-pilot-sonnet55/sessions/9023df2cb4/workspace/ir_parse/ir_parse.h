/*
 * Streaming parser for Flipper ".ir" remote files (Filetype "IR signals file",
 * Version 1). Plain C99, no malloc, no stdio, no Flipper headers, so the same
 * source builds in a FAP and in a PC test binary.
 *
 * Memory is fixed at sizeof(IrParser) (about 7 KB). Raw timings are validated
 * and counted but not stored. Every input is bounded by the IR_MAX_* limits;
 * the first violation puts the parser in a sticky error state and all later
 * input is ignored.
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command,
 * frequency, duty_cycle, data. Any other key is rejected.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IR_MAX_BUTTONS 64u /* signals kept per file */
#define IR_NAME_MAX 63u /* chars, excluding NUL */
#define IR_PROTOCOL_MAX 23u
#define IR_MAX_TIMINGS 1024u /* per raw signal (firmware limit) */
#define IR_MAX_LINE 16384u /* bytes per line */
#define IR_MAX_FILE (256u * 1024u) /* bytes per file */
#define IR_MIN_FREQUENCY 10000u
#define IR_MAX_FREQUENCY 56000u
#define IR_VALUE_MAX 95u
#define IR_KEY_MAX 15u

typedef enum {
    IrOk = 0,
    IrErrFiletype, /* missing/incorrect "Filetype: IR signals file" (also empty file) */
    IrErrVersion, /* Version is not 1 */
    IrErrSyntax, /* line is not "key: value" / "#" / blank */
    IrErrBadChar, /* control or non-ASCII byte in a value */
    IrErrLineTooLong,
    IrErrFileTooLarge,
    IrErrUnsupportedKey,
    IrErrDuplicateKey,
    IrErrKeyBeforeName, /* a field appeared before the first "name:" */
    IrErrBadValue, /* malformed or empty value, bad hex, short address... */
    IrErrOverflow, /* number does not fit in uint32 */
    IrErrRange, /* frequency or duty_cycle outside allowed range */
    IrErrBadName,
    IrErrTooManyTimings,
    IrErrTooManyButtons,
    IrErrIncomplete, /* signal lacks a required key, or mixes parsed/raw keys */
    IrErrTruncated, /* file ends after the header with no signals */
} IrStatus;

typedef enum {
    IrTypeParsed,
    IrTypeRaw,
} IrSignalType;

typedef struct {
    char name[IR_NAME_MAX + 1]; /* printable ASCII only, safe to draw */
    IrSignalType type;
    char protocol[IR_PROTOCOL_MAX + 1]; /* parsed only */
    uint8_t address[4]; /* parsed only */
    uint8_t command[4]; /* parsed only */
    uint32_t frequency; /* raw only, Hz */
    uint32_t duty_cycle_ppm; /* raw only, 1..1000000 (0.33 -> 330000) */
    uint16_t timing_count; /* raw only */
} IrButton;

typedef struct {
    /* --- results (valid for buttons[0..count-1] even after an error) --- */
    IrButton buttons[IR_MAX_BUTTONS];
    size_t count;
    IrStatus status;
    uint32_t error_line; /* 1-based, 0 if no error */

    /* --- internal state --- */
    uint8_t state;
    uint8_t stage; /* 0: expect Filetype, 1: expect Version, 2: body */
    uint8_t key_id;
    uint8_t seen; /* bitmask of keys seen in the open signal */
    bool open; /* a signal is being built in `cur` */
    uint32_t line_no;
    uint32_t line_len;
    uint32_t total;
    uint8_t key_len;
    uint8_t val_len;
    uint8_t tok_len;
    char key[IR_KEY_MAX + 1];
    char val[IR_VALUE_MAX + 1];
    char tok[11];
    IrButton cur;
} IrParser;

void ir_parser_init(IrParser* p);

/* Feed any chunk size (1 byte up to whole file). Returns false once failed. */
bool ir_parser_feed(IrParser* p, const char* data, size_t len);

/* Call at end of input. Returns true only if the whole file was valid. */
bool ir_parser_finish(IrParser* p);

/* One-shot helper for tests and small buffers. */
IrStatus ir_parse_buffer(IrParser* p, const char* data, size_t len);

const char* ir_status_str(IrStatus s);
