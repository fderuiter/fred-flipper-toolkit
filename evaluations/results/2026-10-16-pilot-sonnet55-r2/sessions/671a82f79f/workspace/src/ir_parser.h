#pragma once
/*
 * Bounded parser for a subset of Flipper `.ir` files
 * (Filetype: IR signals file, Version: 1).
 *
 * Plain C99, no Furi/Flipper dependencies, no heap allocation: all memory is
 * in the caller-owned IrParser and IrFile structs, so it builds on a PC for
 * tests and in a FAP alike. Input is pushed in arbitrary chunks, so a FAP can
 * read the file with a small buffer and never hold the whole file in RAM.
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command,
 * frequency, duty_cycle, data. Any other key is rejected, as are duplicate
 * keys inside one signal, non-ASCII/control characters, over-long lines and
 * out-of-range numbers. The first error is sticky; nothing is parsed after it.
 *
 * Raw timings are validated and counted but not stored (the viewer only needs
 * the count), which keeps IrFile small and fixed-size.
 *
 * IrFile is ~8 KiB: allocate it once (static or heap), not on a FAP stack.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IR_MAX_FILE_BYTES (512u * 1024u)
#define IR_MAX_LINE       16384u
#define IR_MAX_SIGNALS    64u
#define IR_MAX_TIMINGS    1024u
#define IR_MAX_NAME       64u
#define IR_MAX_PROTOCOL   32u
#define IR_MIN_FREQUENCY  10000u
#define IR_MAX_FREQUENCY  56000u

typedef enum {
    IrOk = 0,
    IrErrHeader, /* missing/wrong Filetype line */
    IrErrVersion, /* Version is not 1 */
    IrErrTruncated, /* input ended before the two header lines */
    IrErrBadChar, /* NUL, control or non-ASCII byte */
    IrErrLineTooLong,
    IrErrFileTooBig,
    IrErrSyntax, /* line is not `key: value` */
    IrErrUnknownKey,
    IrErrDuplicateKey,
    IrErrBadValue, /* malformed or out-of-range value */
    IrErrMissingKey, /* signal lacks a required key */
    IrErrWrongFields, /* key not allowed for this signal type */
    IrErrTooManySignals,
    IrErrTooManyTimings,
} IrError;

typedef enum {
    IrSignalParsed,
    IrSignalRaw,
} IrSignalType;

typedef struct {
    char name[IR_MAX_NAME + 1];
    IrSignalType type;
    /* parsed */
    char protocol[IR_MAX_PROTOCOL + 1];
    uint8_t address[4];
    uint8_t command[4];
    /* raw */
    uint32_t frequency; /* Hz */
    uint32_t duty_cycle_ppm; /* 330000 == 0.33 */
    uint32_t timing_count;
} IrSignal;

typedef struct {
    IrSignal signals[IR_MAX_SIGNALS];
    size_t count;
} IrFile;

typedef struct {
    IrFile* out;
    IrError error;
    size_t error_line; /* 1-based, 0 if none */
    size_t line_no;
    size_t total_bytes;
    size_t line_len;
    char line[IR_MAX_LINE + 1];
    bool in_signal;
    uint32_t seen; /* bitmask of keys seen in current signal */
    IrSignal cur;
} IrParser;

/* Resets the parser and clears *out. */
void ir_parser_init(IrParser* p, IrFile* out);

/* Pushes bytes. Returns IrOk while input is acceptable, else the sticky error. */
IrError ir_parser_feed(IrParser* p, const uint8_t* data, size_t len);

/* Signals end of input and finalizes the last signal. */
IrError ir_parser_finish(IrParser* p);

/* Convenience: init + feed(all) + finish. */
IrError ir_parse_buffer(IrParser* p, IrFile* out, const uint8_t* data, size_t len);

const char* ir_error_string(IrError e);
