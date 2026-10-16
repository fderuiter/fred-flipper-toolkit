/*
 * Streaming parser for Flipper ".ir" remote files (Filetype: IR signals file, Version: 1).
 *
 * Pure C99, no Flipper SDK headers, no heap allocation, no libc I/O: the same
 * source builds in a FAP and in a PC test binary. All memory is fixed-size and
 * owned by the caller. Raw timings are counted, not stored.
 *
 * Usage (see README.md):
 *     ir_signal_t signals[IR_MAX_SIGNALS];
 *     ir_parser_t p;
 *     ir_parser_init(&p, signals, IR_MAX_SIGNALS);
 *     while ((n = read_chunk(buf, sizeof buf)) > 0)
 *         if (ir_parser_feed(&p, buf, n) != IR_OK) break;
 *     ir_status_t st = ir_parser_finish(&p);
 *     // signals[0 .. ir_parser_count(&p)-1] are complete and valid, even if st != IR_OK
 *
 * Errors are sticky: after the first error all further input is ignored.
 */
#ifndef IR_PARSE_H
#define IR_PARSE_H

#include <stddef.h>
#include <stdint.h>

/* Hard limits. Anything beyond them is rejected, never truncated. */
#define IR_MAX_SIGNALS 64u      /* buttons per file                               */
#define IR_NAME_MAX 63u         /* chars in a button name (printable ASCII only)  */
#define IR_PROTOCOL_MAX 23u     /* chars in a protocol name                       */
#define IR_MAX_TIMINGS 1024u    /* raw timings per signal                         */
#define IR_FREQ_MIN 10000u      /* Hz                                             */
#define IR_FREQ_MAX 56000u      /* Hz                                             */
#define IR_LINE_VALUE_MAX 95u   /* chars in any non-`data` value; longer lines fail */

typedef enum {
    IR_OK = 0,
    IR_ERR_EMPTY,               /* no header at all                               */
    IR_ERR_BAD_FILETYPE,        /* first line is not "Filetype: IR signals file"  */
    IR_ERR_UNSUPPORTED_VERSION, /* Version is not 1 (or malformed)                */
    IR_ERR_SYNTAX,              /* line without "key: value"                      */
    IR_ERR_UNKNOWN_KEY,         /* key outside the supported subset               */
    IR_ERR_DUPLICATE_KEY,       /* same key twice in one signal                   */
    IR_ERR_BAD_VALUE,           /* malformed, out of range, or non-printable      */
    IR_ERR_LINE_TOO_LONG,       /* value exceeds IR_LINE_VALUE_MAX                */
    IR_ERR_TOO_MANY_TIMINGS,    /* more than IR_MAX_TIMINGS                       */
    IR_ERR_TOO_MANY_SIGNALS,    /* more than the caller's capacity                */
    IR_ERR_INCOMPLETE           /* signal missing fields / has fields of wrong type */
} ir_status_t;

typedef enum { IR_SIGNAL_PARSED = 0, IR_SIGNAL_RAW = 1 } ir_signal_type_t;

typedef struct {
    char name[IR_NAME_MAX + 1];
    ir_signal_type_t type;
    /* parsed */
    char protocol[IR_PROTOCOL_MAX + 1];
    uint8_t address[4];
    uint8_t command[4];
    /* raw */
    uint32_t frequency;     /* Hz */
    uint32_t duty_ppm;      /* duty cycle in millionths: 0.33 -> 330000 */
    uint32_t timing_count;
} ir_signal_t;

typedef struct {
    ir_signal_t* out;
    size_t capacity;
    size_t count;           /* completed signals */
    ir_status_t status;
    uint32_t error_line;    /* 1-based line of first error, 0 if none */
    uint32_t line;          /* current 1-based line */

    /* internal state */
    uint8_t phase;          /* header progress */
    uint8_t lstate;         /* line scanner state */
    uint8_t key;            /* resolved key id of current line */
    uint8_t in_signal;
    uint16_t seen;          /* bitmask of keys seen in current signal */
    uint8_t klen;
    uint8_t vlen;
    char kbuf[16];
    char vbuf[IR_LINE_VALUE_MAX + 1];
    uint32_t tok;           /* data token accumulator */
    uint8_t tok_digits;
} ir_parser_t;

/* `out` must hold `capacity` entries; entries past ir_parser_count() are scratch. */
void ir_parser_init(ir_parser_t* p, ir_signal_t* out, size_t capacity);

/* Feed any chunk of bytes (any size, may split lines/tokens anywhere). Returns p->status. */
ir_status_t ir_parser_feed(ir_parser_t* p, const char* data, size_t len);

/* Call once at end of input. Flushes a final line without '\n' and the last signal. */
ir_status_t ir_parser_finish(ir_parser_t* p);

size_t ir_parser_count(const ir_parser_t* p);

const char* ir_status_str(ir_status_t s);

/* Convenience for whole-buffer input (tests, PC tool). */
ir_status_t ir_parse_buffer(
    const char* data,
    size_t len,
    ir_signal_t* out,
    size_t capacity,
    size_t* count,
    uint32_t* error_line);

#endif
