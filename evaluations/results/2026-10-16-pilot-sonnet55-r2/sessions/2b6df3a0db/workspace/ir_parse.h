#pragma once
/*
 * Bounded parser for a subset of Flipper `.ir` files (Filetype "IR signals file", Version 1).
 * Plain C99, no Furi/FAP dependencies, no heap use: the caller supplies the output array and
 * a byte-source callback. The parser never stores raw timings, it validates and counts them.
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command, frequency, duty_cycle, data.
 * Any other key is rejected (IrErrUnsupportedKey). Lines starting with '#' are ignored.
 *
 * On error, buttons[0..*count) hold the records completed before the failure; the failing
 * record is not included. Treat the file as invalid when status != IrOk.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define IR_MAX_NAME_LEN      31 /* longer names are truncated, IrButton.name_truncated is set */
#define IR_MAX_PROTOCOL_LEN  15
#define IR_MAX_TIMINGS       1024
#define IR_MIN_FREQUENCY     10000U
#define IR_MAX_FREQUENCY     1000000U
#define IR_MAX_VALUE_LEN     63 /* longest buffered (non-data) value */
#define IR_MAX_FILE_BYTES    (512U * 1024U)

typedef enum {
    IrTypeParsed = 1,
    IrTypeRaw = 2,
} IrSignalType;

typedef struct {
    char name[IR_MAX_NAME_LEN + 1];
    bool name_truncated;
    IrSignalType type;
    /* parsed */
    char protocol[IR_MAX_PROTOCOL_LEN + 1];
    uint8_t address[4];
    uint8_t command[4];
    /* raw */
    uint32_t frequency;
    uint16_t duty_permille; /* 1..1000 */
    uint16_t timing_count; /* 1..IR_MAX_TIMINGS */
} IrButton;

typedef enum {
    IrOk = 0,
    IrErrIo,
    IrErrEmptyFile,
    IrErrFileTooLarge,
    IrErrBadFiletype,
    IrErrBadVersion, /* header Version is not 1 (including future versions) */
    IrErrBadCharacter, /* control byte or NUL in the file */
    IrErrSyntax, /* line without "key: value" or key outside a record */
    IrErrLineTooLong,
    IrErrUnsupportedKey,
    IrErrDuplicateKey,
    IrErrKeyOrder, /* type-specific key before "type", or wrong for the type */
    IrErrBadValue,
    IrErrMissingField, /* record ended without a required key (also truncated files) */
    IrErrTooManyTimings,
    IrErrTooManyButtons,
} IrStatus;

typedef struct {
    IrStatus status;
    uint32_t line; /* 1-based line of the failure, 0 if none */
} IrResult;

/* Returns number of bytes written to buf (0 = EOF), or a negative value on I/O error.
 * Must not return more than `max`. */
typedef int32_t (*IrReadFn)(void* ctx, uint8_t* buf, size_t max);

IrResult ir_parse(IrReadFn read, void* ctx, IrButton* buttons, size_t capacity, size_t* count);

/* Convenience for in-memory data (tests, already-loaded buffers). */
IrResult ir_parse_buffer(const uint8_t* data, size_t len, IrButton* buttons, size_t capacity, size_t* count);

const char* ir_status_string(IrStatus status);
