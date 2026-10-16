// Parser for Flipper ".ir" files (Filetype "IR signals file", Version 1).
// Pure C, no Flipper SDK dependency, no heap allocation: builds on PC and in a FAP.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define IR_MAX_FILE_SIZE (64u * 1024u) // bytes accepted
#define IR_MAX_LINE_LEN  16384u        // bytes per line, excluding newline (fits 1024 timings)
#define IR_MAX_SIGNALS   64u
#define IR_MAX_NAME_LEN  63u           // printable ASCII only
#define IR_MAX_TIMINGS   1024u         // per raw signal
#define IR_MAX_PROTO_LEN 15u

typedef enum {
    IrOk = 0,
    IrErrTooLarge,
    IrErrEmpty,
    IrErrBadHeader, // wrong or missing Filetype
    IrErrBadVersion, // not Version 1
    IrErrLineTooLong,
    IrErrBadLine, // no "key: value", NUL byte, etc.
    IrErrUnknownKey,
    IrErrDuplicateKey,
    IrErrBadValue, // bad number / hex / overflow / non-printable name
    IrErrTooManySignals,
    IrErrTooManyTimings,
    IrErrIncomplete, // missing required key or truncated record
} IrError;

typedef enum {
    IrSignalParsed,
    IrSignalRaw,
} IrSignalType;

typedef struct {
    char name[IR_MAX_NAME_LEN + 1];
    IrSignalType type;
    // parsed
    char protocol[IR_MAX_PROTO_LEN + 1];
    uint32_t address; // first 4 bytes of the file value, byte 0 least significant
    uint32_t command;
    // raw
    uint32_t frequency; // Hz
    float duty_cycle; // 0..1
    uint32_t timings_count; // timings are counted and validated, not stored
} IrSignal;

typedef struct {
    IrSignal signals[IR_MAX_SIGNALS];
    size_t count;
    IrError error;
    size_t error_line; // 1-based, 0 if not line-specific
} IrFile;

// Parses `len` bytes at `data` (need not be NUL-terminated). Returns IrOk or the
// first error. The parse is all-or-nothing for the caller's purposes, but on error
// `out->count` still holds the signals completed before the error.
IrError ir_parse(const char* data, size_t len, IrFile* out);

const char* ir_error_str(IrError e);
