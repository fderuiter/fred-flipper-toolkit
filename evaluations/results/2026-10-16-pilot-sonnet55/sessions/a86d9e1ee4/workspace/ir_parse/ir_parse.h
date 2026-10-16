#pragma once
/*
 * Bounded parser for Flipper .ir files (Filetype "IR signals file", Version 1).
 * Plain C99, no Furi/OS dependency, no heap allocation: host-testable.
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command,
 * frequency, duty_cycle, data. Any other key is rejected.
 * Raw timings are validated and counted, not stored.
 *
 * Input arrives through a read callback so the FAP can feed it from
 * storage_file_read() (short reads allowed, 0 = EOF, <0 = error) and the
 * tests from memory or stdio. The parser never reads more than
 * IR_MAX_FILE_BYTES in total and never holds more than IR_MAX_LINE bytes.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define IR_MAX_FILE_BYTES (256u * 1024u)
#define IR_MAX_LINE       12288u /* 1024 timings * up to 11 chars + key */
#define IR_MAX_SIGNALS    64u
#define IR_MAX_NAME       64u /* including NUL; longer names are rejected */
#define IR_MAX_PROTOCOL   32u /* including NUL */
#define IR_MAX_TIMINGS    1024u
#define IR_MIN_FREQUENCY  10000u
#define IR_MAX_FREQUENCY  56000u

typedef enum {
    IrParseOk = 0,
    IrParseErrRead, /* read callback failed */
    IrParseErrFileTooLarge,
    IrParseErrLineTooLong,
    IrParseErrBadHeader, /* missing/wrong Filetype */
    IrParseErrBadVersion, /* missing, malformed or unsupported Version */
    IrParseErrTruncated, /* file ends inside a signal / without header */
    IrParseErrUnknownKey,
    IrParseErrDuplicateKey,
    IrParseErrMalformedLine,
    IrParseErrBadName, /* empty, too long, or non-printable */
    IrParseErrBadType,
    IrParseErrBadProtocol,
    IrParseErrBadHex, /* address/command not exactly 4 hex bytes */
    IrParseErrBadNumber, /* out of range / not a number */
    IrParseErrTooManyTimings,
    IrParseErrTooManySignals,
    IrParseErrMissingField, /* signal lacks a required key for its type */
    IrParseErrFieldForType, /* key not valid for this signal type */
} IrParseError;

typedef enum {
    IrSignalParsed,
    IrSignalRaw,
} IrSignalType;

typedef struct {
    char name[IR_MAX_NAME];
    IrSignalType type;
    /* parsed */
    char protocol[IR_MAX_PROTOCOL];
    uint32_t address; /* bytes little-endian as in file: "04 00 00 00" -> 4 */
    uint32_t command;
    /* raw */
    uint32_t frequency;
    float duty_cycle;
    uint32_t timings_count;
} IrSignalInfo;

/* Roughly 20 KB (signals + line scratch): allocate with malloc/free in the
 * FAP, do not put it on a thread stack. */
typedef struct {
    IrSignalInfo signals[IR_MAX_SIGNALS];
    size_t count;
    IrParseError error; /* IrParseOk on success */
    size_t error_line; /* 1-based line of the error, 0 if n/a */
    char line_scratch[IR_MAX_LINE + 1]; /* internal */
} IrParseResult;

/* Returns bytes read (0 = EOF) or a negative value on failure. */
typedef int32_t (*IrReadFn)(void* ctx, uint8_t* buf, size_t max_len);

/* Fills *out (always fully initialised). Returns out->error. On error the
 * signals parsed before the error are discarded (count = 0). */
IrParseError ir_parse(IrReadFn read, void* ctx, IrParseResult* out);

/* Convenience: parse from a memory buffer. */
IrParseError ir_parse_buffer(const uint8_t* data, size_t len, IrParseResult* out);

const char* ir_parse_error_str(IrParseError e);
