#pragma once
/* Pure logic (no Furi dependencies): settings parse/serialize and the interval state machine.
 * Host-testable; see tests/host_test.c. */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define IT_WORK_MIN   5
#define IT_WORK_MAX   600
#define IT_REST_MIN   0
#define IT_REST_MAX   600
#define IT_ROUNDS_MIN 1
#define IT_ROUNDS_MAX 99

#define IT_WORK_DEFAULT   30
#define IT_REST_DEFAULT   10
#define IT_ROUNDS_DEFAULT 8

#define IT_FORMAT_VERSION 2
#define IT_FILE_MAX_SIZE  512
#define IT_LINE_MAX       64

typedef struct {
    uint16_t work_s;
    uint16_t rest_s;
    uint8_t rounds;
    bool vibrate;
} ItSettings;

typedef enum {
    ItParseOk, /* current version, all fields valid */
    ItParseMigrated, /* older known version, values kept, rewrite as current */
    ItParseFuture, /* newer version: known fields best-effort, file must not be overwritten */
    ItParseInvalid, /* corrupt: *out is set to defaults */
} ItParseResult;

void it_settings_defaults(ItSettings* s);

/* Parses `len` bytes (not NUL-terminated). Always leaves *out fully initialized. */
ItParseResult it_settings_parse(const char* text, size_t len, ItSettings* out);

/* Returns bytes written (no NUL needed), or 0 if cap is too small / values out of range. */
size_t it_settings_serialize(const ItSettings* s, char* buf, size_t cap);

typedef enum {
    ItPhaseWork,
    ItPhaseRest,
    ItPhaseDone,
} ItPhase;

typedef struct {
    ItPhase phase;
    uint8_t round; /* 1-based */
    uint32_t ms_left;
} ItSession;

#define IT_STEP_PHASE_CHANGED (1u << 0)
#define IT_STEP_FINISHED      (1u << 1)

void it_session_start(ItSession* s, const ItSettings* cfg);

/* Advances by elapsed_ms (carrying overshoot across phases). Returns IT_STEP_* flags. */
uint8_t it_session_step(ItSession* s, const ItSettings* cfg, uint32_t elapsed_ms);

static inline uint32_t it_session_seconds_left(const ItSession* s) {
    return (s->ms_left + 999u) / 1000u;
}
