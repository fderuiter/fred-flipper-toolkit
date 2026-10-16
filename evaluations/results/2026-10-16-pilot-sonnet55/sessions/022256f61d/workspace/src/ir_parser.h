/*
 * Parser for Flipper Zero ".ir" remote files (Filetype: IR signals file, Version: 1).
 *
 * Pure C99, no Flipper SDK dependency, no heap allocation: the same two files build
 * in a FAP and on a PC. Input is pushed in arbitrary chunks (irv_feed), so the FAP can
 * read the file from storage through a small buffer; nothing but the button list is
 * kept. Raw timings are validated and counted, never stored.
 *
 * Supported keys: name, type (parsed|raw), protocol, address, command, frequency,
 * duty_cycle, data. Any other key is rejected (IRV_ERR_UNKNOWN_KEY).
 *
 * Error policy: parsing stops at the first error. Buttons completed before the error
 * stay valid in result->buttons[0..count), so a viewer can still show them and report
 * the error and its line number.
 */
#ifndef IR_PARSER_H
#define IR_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hard limits (bounded memory and CPU on any input). */
#define IRV_NAME_MAX      31u        /* chars, printable ASCII only */
#define IRV_PROTOCOL_MAX  23u
#define IRV_KEY_MAX       15u
#define IRV_VALUE_MAX     63u        /* longest buffered (non-data) value */
#define IRV_MAX_TIMINGS   1024u      /* raw timings per signal (firmware limit) */
#define IRV_MAX_FILE_BYTES (256u * 1024u)
#define IRV_FREQ_MIN      10000u
#define IRV_FREQ_MAX      56000u

typedef enum {
    IRV_OK = 0,
    IRV_ERR_BAD_HEADER,        /* missing/wrong Filetype or header order, or empty file */
    IRV_ERR_UNSUPPORTED_VERSION,
    IRV_ERR_SYNTAX,            /* line is not "key: value" */
    IRV_ERR_UNKNOWN_KEY,
    IRV_ERR_DUPLICATE_KEY,
    IRV_ERR_ORDER,             /* field before name/type */
    IRV_ERR_WRONG_FIELD_FOR_TYPE,
    IRV_ERR_KEY_TOO_LONG,
    IRV_ERR_VALUE_TOO_LONG,
    IRV_ERR_NAME_TOO_LONG,
    IRV_ERR_BAD_NAME,          /* empty or non-printable/non-ASCII characters */
    IRV_ERR_BAD_VALUE,
    IRV_ERR_BAD_NUMBER,        /* not a plain unsigned decimal */
    IRV_ERR_NUMBER_RANGE,      /* does not fit uint32 / outside allowed range */
    IRV_ERR_BAD_HEX,           /* address/command must be exactly 4 bytes "XX XX XX XX" */
    IRV_ERR_TOO_MANY_TIMINGS,
    IRV_ERR_TOO_MANY_BUTTONS,  /* caller-provided capacity exhausted */
    IRV_ERR_INCOMPLETE,        /* button missing required fields (e.g. truncated file) */
    IRV_ERR_FILE_TOO_LARGE,
    IRV_ERR_IO                 /* reader callback failed */
} irv_error_t;

typedef enum { IRV_SIGNAL_PARSED = 1, IRV_SIGNAL_RAW = 2 } irv_signal_type_t;

typedef struct {
    char name[IRV_NAME_MAX + 1];
    irv_signal_type_t type;
    /* parsed */
    char protocol[IRV_PROTOCOL_MAX + 1];
    uint8_t address[4];
    uint8_t command[4];
    /* raw */
    uint32_t frequency;     /* Hz */
    uint32_t duty_ppm;      /* duty cycle in millionths: 0.33 -> 330000 */
    uint32_t timing_count;  /* number of values in "data" */
} irv_button_t;

typedef struct {
    irv_button_t* buttons;  /* caller-owned array */
    size_t capacity;
    size_t count;           /* completed, valid buttons */
    irv_error_t error;      /* IRV_OK if everything parsed */
    uint32_t error_line;    /* 1-based line of the error (0 if none) */
} irv_result_t;

/* Incremental parser. Treat as opaque; initialise with irv_init. ~250 bytes. */
typedef struct {
    irv_result_t* res;
    irv_button_t cur;
    uint32_t line;
    uint32_t button_line;
    uint32_t total;
    uint32_t token;
    uint16_t seen;
    uint8_t state;
    uint8_t header;
    uint8_t keylen;
    uint8_t vallen;
    bool in_button;
    bool token_active;
    char key[IRV_KEY_MAX + 1];
    char val[IRV_VALUE_MAX + 1];
} irv_parser_t;

void irv_init(irv_parser_t* p, irv_result_t* res, irv_button_t* storage, size_t capacity);
/* Returns false once an error has occurred (also stored in res); further input is ignored. */
bool irv_feed(irv_parser_t* p, const uint8_t* data, size_t len);
/* Call at end of input. Returns true iff the whole file was valid. */
bool irv_finish(irv_parser_t* p);

/* Convenience: parse a complete in-memory buffer. */
bool irv_parse_buffer(const uint8_t* data, size_t len, irv_result_t* res,
                      irv_button_t* storage, size_t capacity);

/* Convenience: pull from a reader. read() returns bytes read (0 = EOF), or
 * (size_t)-1 on failure. Reads through a small stack buffer. */
typedef size_t (*irv_read_fn)(void* ctx, uint8_t* buf, size_t max);
bool irv_parse_reader(irv_read_fn read, void* ctx, irv_result_t* res,
                      irv_button_t* storage, size_t capacity);

const char* irv_error_str(irv_error_t e);

/* One-line summary for the UI, always NUL-terminated, truncated to fit:
 *   parsed: "NEC"          raw: "raw 38000Hz, 11 timings"
 * Returns the number of characters that would have been written (like snprintf). */
size_t irv_describe(const irv_button_t* b, char* out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
