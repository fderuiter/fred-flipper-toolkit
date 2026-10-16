#pragma once
/* Bounded parser for Flipper `.ir` files (Filetype "IR signals file", Version 1).
 * Pure C, no Furi/heap dependencies: host-testable, fixed-size result.
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command,
 * frequency, duty_cycle, data. Any other key rejects the file.
 * Raw timings are validated and counted, not stored.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define IR_MAX_SIGNALS      64u
#define IR_MAX_NAME_LEN     63u      /* excluding NUL */
#define IR_MAX_PROTOCOL_LEN 31u
#define IR_MAX_LINE_LEN     8192u    /* 1024 timings * up to 7 digits + sep, with margin */
#define IR_MAX_FILE_BYTES   (256u * 1024u)
#define IR_MAX_RAW_TIMINGS  1024u
#define IR_MAX_TIMING_US    1000000u /* per-timing sanity limit */
#define IR_MIN_FREQUENCY    10000u
#define IR_MAX_FREQUENCY    56000u

typedef enum {
    IrSignalParsed,
    IrSignalRaw,
} IrSignalType;

typedef enum {
    IrParseOk = 0,
    IrParseErrNullArg,
    IrParseErrEmpty,
    IrParseErrFileTooLarge,
    IrParseErrLineTooLong,
    IrParseErrBadHeader,      /* wrong/missing Filetype */
    IrParseErrBadVersion,     /* missing, malformed or unsupported Version */
    IrParseErrSyntax,         /* line without "key: value" */
    IrParseErrUnknownKey,
    IrParseErrDuplicateKey,
    IrParseErrBadValue,       /* range, charset, length */
    IrParseErrTooManyTimings,
    IrParseErrTooManySignals,
    IrParseErrTruncated,      /* final signal incomplete */
    IrParseErrIncomplete,     /* signal missing required fields */
} IrParseError;

typedef struct {
    char name[IR_MAX_NAME_LEN + 1];
    IrSignalType type;
    /* parsed */
    char protocol[IR_MAX_PROTOCOL_LEN + 1];
    uint8_t address[4];
    uint8_t command[4];
    /* raw */
    uint32_t frequency;
    float duty_cycle;
    uint32_t timing_count;
} IrSignal;

typedef struct {
    size_t count;
    IrSignal signals[IR_MAX_SIGNALS];
} IrFile;

typedef struct {
    IrParseError error;
    size_t line; /* 1-based line of the error, 0 if n/a */
} IrParseResult;

/* Parses `len` bytes at `buf` (need not be NUL-terminated). `out` is fully
 * overwritten; on error it holds only the signals completed before the error. */
IrParseResult ir_parse(const char* buf, size_t len, IrFile* out);

const char* ir_parse_error_str(IrParseError e);
