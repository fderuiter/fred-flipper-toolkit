#pragma once
/*
 * Bounded parser for the Flipper `.ir` subset (Filetype "IR signals file", Version 1).
 *
 * Plain C99, no Furi/FAP dependencies, no heap use: all storage is in the caller's
 * IrRemote and a small stack state. Input is pulled through a read callback in small
 * chunks, so the caller never needs to hold the whole file.
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command, frequency,
 * duty_cycle, data. Any other key, duplicate key, or key not valid for the signal's
 * type rejects the whole file. Raw timings are validated and counted, not stored.
 */
#include <stddef.h>
#include <stdint.h>

#define IR_MAX_SIGNALS      64u      /* buttons per file */
#define IR_MAX_NAME_LEN     63u      /* chars, excluding NUL */
#define IR_MAX_PROTOCOL_LEN 23u
#define IR_MAX_TIMINGS      1024u    /* documented raw maximum */
#define IR_MAX_INPUT_BYTES  (128u * 1024u) /* total bytes consumed before giving up */
#define IR_MAX_VALUE_LEN    96u      /* longest non-`data` value */
#define IR_MIN_FREQUENCY    10000u   /* Hz; assumed to match firmware limits, not verified here */
#define IR_MAX_FREQUENCY    56000u
#define IR_READ_ERROR       ((size_t)-1)

typedef enum {
    IrOk = 0,
    IrErrIo,              /* read callback failed or misbehaved */
    IrErrInputTooLarge,   /* more than IR_MAX_INPUT_BYTES */
    IrErrBadHeader,       /* missing/wrong Filetype or Version line */
    IrErrUnsupportedVersion,
    IrErrMalformedLine,   /* no ':' , non-printable byte */
    IrErrLineTooLong,
    IrErrUnknownKey,
    IrErrDuplicateKey,
    IrErrBadValue,        /* number out of range, bad hex, bad type */
    IrErrTooManyTimings,
    IrErrTooManySignals,
    IrErrIncompleteSignal /* missing required key, or key invalid for the type */
} IrError;

typedef enum { IrSignalParsed = 1, IrSignalRaw = 2 } IrSignalType;

typedef struct {
    char name[IR_MAX_NAME_LEN + 1];
    IrSignalType type;
    /* parsed */
    char protocol[IR_MAX_PROTOCOL_LEN + 1];
    uint8_t address[4];
    uint8_t command[4];
    /* raw */
    uint32_t frequency;     /* Hz */
    uint32_t duty_ppm;      /* duty cycle in parts per million, (0, 1000000] */
    uint32_t timing_count;  /* number of validated timings */
} IrSignal;

typedef struct {
    size_t count;
    IrSignal signals[IR_MAX_SIGNALS];
    IrError error;      /* IrOk on success. On failure `signals[0..count)` are the signals
                           completed before the error; do not present them as a valid remote. */
    size_t error_line;  /* 1-based line of the error, 0 if not line related */
} IrRemote;

/* Return bytes written to buf (<= cap), 0 at end of input, IR_READ_ERROR on failure. */
typedef size_t (*IrReadFn)(void* ctx, uint8_t* buf, size_t cap);

/* Parses a whole stream. Always initializes *out. Returns out->error. */
IrError ir_parse_stream(IrReadFn read, void* ctx, IrRemote* out);

/* Convenience wrapper for a memory buffer. */
IrError ir_parse_buffer(const uint8_t* data, size_t len, IrRemote* out);

const char* ir_error_str(IrError e);
