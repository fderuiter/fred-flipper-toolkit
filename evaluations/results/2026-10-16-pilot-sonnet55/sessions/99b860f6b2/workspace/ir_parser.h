/*
 * Parser for Flipper ".ir" remote files (Filetype "IR signals file", Version 1).
 *
 * Pure C99, no Flipper SDK includes, no heap allocation: the same source builds
 * into the FAP and into the PC tests. All memory is the caller's IrFile plus a
 * small fixed stack footprint (~200 bytes).
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command,
 * frequency, duty_cycle, data. Any other key rejects the file.
 *
 * Policy: the first problem aborts parsing; the file is then rejected as a
 * whole (out->count is 0, out->error / out->error_line say why).
 * Raw timings are validated and counted but NOT stored, so memory use does not
 * depend on the input.
 */
#ifndef IR_PARSER_H
#define IR_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IR_MAX_SIGNALS   64u   /* more buttons than this -> IR_ERR_TOO_MANY_SIGNALS */
#define IR_NAME_SIZE     32u   /* incl. NUL; longer names are truncated, flagged */
#define IR_PROTOCOL_SIZE 16u   /* incl. NUL; longer protocol names are rejected */
#define IR_MAX_LINE      128u  /* longest non-"data" line accepted (bytes) */
#define IR_MAX_TIMINGS   1024u /* matches Flipper firmware's raw limit */
#define IR_MIN_FREQUENCY 10000u
#define IR_MAX_FREQUENCY 56000u

typedef enum {
    IrErrNone = 0,
    IrErrEmpty,          /* no header at all */
    IrErrBadHeader,      /* Filetype/Version missing or not "IR signals file" */
    IrErrBadVersion,     /* Version is not 1 */
    IrErrLineTooLong,    /* a non-data line exceeds IR_MAX_LINE */
    IrErrNonPrintable,   /* control character in a line */
    IrErrSyntax,         /* line is not "key: value" */
    IrErrUnknownKey,
    IrErrDuplicateKey,   /* key repeated within one signal */
    IrErrBadType,
    IrErrBadValue,       /* name/protocol/frequency/duty_cycle malformed or out of range */
    IrErrBadHex,         /* address/command is not exactly 4 hex bytes */
    IrErrBadTiming,      /* non-numeric, negative or > UINT32_MAX timing */
    IrErrTooManyTimings, /* more than IR_MAX_TIMINGS */
    IrErrIncomplete,     /* signal lacks a required key, or has a key of the other type */
    IrErrTooManySignals,
    IrErrTruncated,      /* file ends without a final newline */
    IrErrRead            /* read callback reported failure */
} IrError;

typedef enum { IrTypeParsed = 0, IrTypeRaw } IrSignalType;

typedef struct {
    char name[IR_NAME_SIZE];
    bool name_truncated;
    IrSignalType type;
    /* parsed */
    char protocol[IR_PROTOCOL_SIZE];
    uint8_t address[4];
    uint8_t command[4];
    /* raw */
    uint32_t frequency;     /* Hz */
    uint16_t duty_permille; /* 330 == 0.33 */
    uint16_t timing_count;
} IrSignal;

typedef struct {
    IrSignal signals[IR_MAX_SIGNALS];
    size_t count;
    IrError error;
    size_t error_line; /* 1-based; 0 if not applicable */
} IrFile;

/* Read up to `max` bytes into buf. Return bytes read, 0 at EOF, or (size_t)-1 on error. */
typedef size_t (*IrReadFn)(void* ctx, uint8_t* buf, size_t max);

/* Returns true on success. `out` is fully (re)initialised. */
bool ir_parse(IrReadFn read, void* ctx, IrFile* out);
bool ir_parse_buffer(const uint8_t* data, size_t size, IrFile* out);

const char* ir_error_str(IrError e);

#endif
