#pragma once
/*
 * Bounded parser for Flipper Infrared remote files (.ir)
 *   Filetype: IR signals file
 *   Version: 1
 *
 * Plain C99, no Furi / Flipper dependencies, so it builds on a PC for tests and
 * inside a FAP unchanged. It performs no dynamic allocation: all memory is the
 * IrFile and IrParser structs the caller provides (allocate them on the heap in
 * a FAP, IrFile is ~13 KiB).
 *
 * Supported subset (anything else is rejected, not skipped):
 *   keys: name, type (parsed|raw), protocol, address, command,
 *         frequency, duty_cycle, data
 *   parsed signal: protocol, address (4 hex bytes), command (4 hex bytes)
 *   raw signal:    frequency, duty_cycle, data (1..IR_MAX_TIMINGS timings)
 *   '#' comment lines and blank lines are ignored.
 *
 * Raw timings are only counted, never stored, so memory does not grow with the
 * file. Input is fed in arbitrary chunks (e.g. 256 bytes from a storage read);
 * results do not depend on chunk boundaries.
 *
 * Error policy: the first error is sticky and stops parsing. Buttons that were
 * completely valid before the error stay in IrFile (partial list), the failing
 * button is never added. Decide in the UI whether to show them; the status and
 * line number say what went wrong.
 *
 * Typical FAP use (storage read loop, in a worker, not in a draw callback):
 *
 *   IrParser* p = malloc(sizeof(IrParser)); IrFile* f = malloc(sizeof(IrFile));
 *   ir_parser_init(p, f);
 *   while((n = storage_file_read(file, buf, sizeof(buf))) > 0)
 *       if(ir_parser_feed(p, buf, n) != IrStatusOk) break;
 *   IrStatus st = ir_parser_finish(p);
 */

#include <stddef.h>
#include <stdint.h>

#define IR_MAX_BUTTONS       128u
#define IR_NAME_MAX          64u /* chars, printable ASCII */
#define IR_PROTOCOL_MAX      15u
#define IR_MAX_TIMINGS       1024u /* documented .ir raw maximum */
#define IR_MIN_FREQUENCY     10000u
#define IR_MAX_FREQUENCY     56000u
#define IR_LINE_MAX          128u /* longest non-data line, excluding newline */
#define IR_MAX_FILE_BYTES    (256u * 1024u)
#define IR_SUMMARY_MAX       48u

typedef enum {
    IrStatusOk = 0,
    IrStatusEmpty, /* no header at all */
    IrStatusBadFiletype,
    IrStatusBadVersion, /* missing, unparsable or not 1 */
    IrStatusBadHeader, /* header lines out of shape */
    IrStatusLineTooLong,
    IrStatusBadChar, /* control or non-ASCII byte */
    IrStatusSyntax, /* line is not "key: value" */
    IrStatusUnsupportedKey,
    IrStatusDuplicateKey,
    IrStatusBadOrder, /* e.g. key before name/type, data outside raw */
    IrStatusBadValue, /* wrong format, overflow, out of range, short hex */
    IrStatusMissingField, /* signal ended without a required key */
    IrStatusTooManyTimings,
    IrStatusTooManyButtons,
    IrStatusFileTooLarge,
} IrStatus;

typedef enum {
    IrTypeParsed,
    IrTypeRaw,
} IrSignalType;

typedef struct {
    char name[IR_NAME_MAX + 1];
    IrSignalType type;
    char protocol[IR_PROTOCOL_MAX + 1]; /* parsed only, else "" */
    uint8_t address[4]; /* parsed only */
    uint8_t command[4]; /* parsed only */
    uint32_t frequency; /* raw only, Hz */
    uint32_t duty_ppm; /* raw only, duty cycle in millionths (0.33 -> 330000) */
    uint32_t timing_count; /* raw only */
} IrButton;

typedef struct {
    size_t count;
    IrButton buttons[IR_MAX_BUTTONS];
} IrFile;

typedef struct {
    IrFile* out;
    IrStatus status;
    uint32_t error_line; /* 1-based, valid when status != Ok */

    /* internal state */
    uint32_t line_no;
    size_t total_bytes;
    uint8_t header_stage; /* 0 Filetype, 1 Version, 2 body */
    uint8_t cur_open;
    uint8_t seen; /* bitmask of keys seen in current signal */
    uint8_t finished;
    uint8_t in_data;
    uint8_t data_started;
    uint8_t tok_len;
    char tok[11];
    size_t line_len;
    char line[IR_LINE_MAX + 1];
    IrButton cur;
} IrParser;

/* `out` is reset. Both pointers must stay valid while parsing. */
void ir_parser_init(IrParser* p, IrFile* out);

/* Feed a chunk. Returns the sticky status (IrStatusOk while still fine). */
IrStatus ir_parser_feed(IrParser* p, const uint8_t* data, size_t len);

/* Signal end of input; validates the last (possibly unterminated) signal. */
IrStatus ir_parser_finish(IrParser* p);

/* One-shot helper for buffers (tests, small files). */
IrStatus ir_parse_buffer(const uint8_t* data, size_t len, IrFile* out, uint32_t* error_line);

const char* ir_status_str(IrStatus s);

/* "Parsed NEC" or "Raw 38000Hz 11 timings"; always NUL-terminated. */
void ir_button_summary(const IrButton* b, char* buf, size_t buf_size);
