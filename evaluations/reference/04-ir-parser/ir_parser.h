// Reviewer-side reference solution for FRED task 04. MIT licensed, original.
// Bounded parser for a subset of Flipper Infrared remote files (.ir, Version 1).
// Pure C99, no Furi dependency, so it can be host-tested.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define IR_MAX_FILE_BYTES   (64u * 1024u)
#define IR_MAX_LINE         256u    /* any key except data */
#define IR_MAX_DATA_LINE    (IR_MAX_TIMINGS * 11u + 8u) /* "data: " + up to 1024 uint32 */
#define IR_MAX_SIGNALS      64u
#define IR_MAX_TIMINGS      1024u   /* per signal, from InfraredFileFormats.md */
#define IR_MAX_TIMING_POOL  4096u   /* all raw timings in one file */
#define IR_NAME_MAX         32u
#define IR_PROTOCOL_MAX     16u

typedef enum { IrSignalParsed, IrSignalRaw } IrSignalType;

typedef struct {
    char name[IR_NAME_MAX + 1];
    IrSignalType type;
    char protocol[IR_PROTOCOL_MAX + 1];
    uint8_t address[4];
    uint8_t command[4];
    uint32_t frequency;
    uint32_t duty_permille;  /* duty cycle * 1000 */
    uint32_t timing_offset;  /* index into IrRemote.timings */
    uint32_t timing_count;
} IrSignal;

typedef struct {
    IrSignal signals[IR_MAX_SIGNALS];
    uint32_t signal_count;
    uint32_t timings[IR_MAX_TIMING_POOL];
    uint32_t timing_used;
} IrRemote;

typedef enum {
    IrOk = 0,
    IrErrTooLarge,
    IrErrHeader,
    IrErrVersion,
    IrErrLineTooLong,
    IrErrSyntax,
    IrErrUnknownKey,
    IrErrDuplicateKey,
    IrErrName,
    IrErrType,
    IrErrHex,
    IrErrNumber,
    IrErrRange,
    IrErrMissingField,
    IrErrTooManySignals,
    IrErrTooManyTimings,
} IrError;

typedef struct {
    IrError code;
    uint32_t line; /* 1-based line of the error, 0 if not line-specific */
} IrResult;

/* Parse `len` bytes. Never reads beyond `len`, never allocates. On error the
 * contents of `out` are unspecified and must not be used. */
IrResult ir_parse(const char* data, size_t len, IrRemote* out);
const char* ir_error_name(IrError code);
