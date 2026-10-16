#pragma once
/* Pure logic: no Furi dependencies, host-testable. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define IT_WORK_MIN   5
#define IT_WORK_MAX   600
#define IT_REST_MIN   0
#define IT_REST_MAX   600
#define IT_ROUNDS_MIN 1
#define IT_ROUNDS_MAX 99

#define IT_FILE_MAX_SIZE 512 /* files larger than this are rejected */
#define IT_FORMAT_VERSION 2

typedef struct {
    uint16_t work;
    uint16_t rest;
    uint8_t rounds;
    bool vibrate;
} ItSettings;

typedef enum {
    ItLoadOk, /* parsed; all fields valid or defaulted individually */
    ItLoadMigrated, /* older version accepted; caller should re-save as v2 */
    ItLoadRepaired, /* some fields invalid/missing and replaced by defaults */
    ItLoadFutureVersion, /* version > IT_FORMAT_VERSION: defaults, caller must NOT overwrite */
    ItLoadBadFile, /* wrong filetype / no version / too large: defaults */
} ItLoadResult;

void it_settings_defaults(ItSettings* s);

/* Parse `len` bytes of text. `s` is always left fully valid. */
ItLoadResult it_settings_parse(const char* data, size_t len, ItSettings* s);

/* Serialize as v2. Returns bytes written (excluding NUL), 0 if buf too small. */
size_t it_settings_format(const ItSettings* s, char* buf, size_t cap);

/* ---- countdown state machine ---- */
typedef enum {
    ItPhaseWork,
    ItPhaseRest,
    ItPhaseDone,
} ItPhase;

typedef struct {
    ItSettings cfg;
    ItPhase phase;
    uint16_t remaining; /* seconds left in phase */
    uint8_t round; /* 1-based */
} ItTimer;

typedef enum {
    ItEventNone,
    ItEventPhaseChange, /* work<->rest */
    ItEventDone,
} ItEvent;

void it_timer_start(ItTimer* t, const ItSettings* cfg);
/* Advance by exactly one second. */
ItEvent it_timer_step(ItTimer* t);
