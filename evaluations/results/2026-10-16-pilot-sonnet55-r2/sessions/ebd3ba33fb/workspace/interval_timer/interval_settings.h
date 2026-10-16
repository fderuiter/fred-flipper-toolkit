#pragma once
/* Pure C (no Furi): settings model, parser/migration, store, run state machine.
 * Host-testable; see tests/test_settings.c. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define IT_WORK_MIN   5
#define IT_WORK_MAX   600
#define IT_REST_MIN   0
#define IT_REST_MAX   600
#define IT_ROUNDS_MIN 1
#define IT_ROUNDS_MAX 99

#define IT_FILETYPE   "Interval Timer Settings"
#define IT_VERSION    2
#define IT_FILE_MAX   256 /* bytes accepted by the parser */
#define IT_LINE_MAX   64
#define IT_LINES_MAX  16
#define IT_PATH_MAX   96

typedef struct {
    uint16_t work_s;
    uint16_t rest_s;
    uint8_t rounds;
    bool vibrate;
} ItSettings;

typedef enum {
    ItParseOk,
    ItParseMigratedV1,
    ItParseFuture, /* Version > IT_VERSION: do not interpret, do not overwrite */
    ItParseCorrupt,
} ItParseResult;

void it_settings_defaults(ItSettings* s);
ItParseResult it_settings_parse(const char* buf, size_t len, ItSettings* out);
/* Returns bytes written (excluding NUL) or 0 if cap is too small. */
size_t it_settings_format(const ItSettings* s, char* buf, size_t cap);

/* Variable-item index mapping. Indexes are uint8_t (max 255 values), so the
 * range is 1 s steps up to 60 s, then 5 s steps. */
uint8_t it_secs_count(uint16_t min, uint16_t max);
uint16_t it_secs_from_index(uint8_t idx, uint16_t min, uint16_t max);
uint8_t it_secs_to_index(uint16_t secs, uint16_t min, uint16_t max);

/* ---- storage abstraction ---- */
typedef enum { ItIoOk, ItIoNotFound, ItIoError } ItIo;

typedef struct {
    /* Reads up to cap bytes; *len = bytes read. */
    ItIo (*read)(void* ctx, const char* path, char* buf, size_t cap, size_t* len);
    /* Create/truncate, write all, sync, close; any short write/failure = ItIoError. */
    ItIo (*write)(void* ctx, const char* path, const char* data, size_t len);
    ItIo (*rename)(void* ctx, const char* from, const char* to);
    ItIo (*remove)(void* ctx, const char* path);
} ItStoreOps;

typedef enum {
    ItLoadFresh, /* no file: defaults */
    ItLoadOk,
    ItLoadMigrated, /* read from v1; file rewritten as v2 on next save */
    ItLoadRecovered, /* main file missing/bad, restored from .tmp/.bak */
    ItLoadFuture, /* newer file: defaults, saving blocked */
    ItLoadCorrupt, /* unparsable: defaults, bad file kept as .bad on next save */
    ItLoadIoError, /* could not read: defaults, saving blocked */
} ItLoadStatus;

typedef struct {
    const ItStoreOps* ops;
    void* ctx;
    char main_path[IT_PATH_MAX];
    char tmp_path[IT_PATH_MAX];
    char bak_path[IT_PATH_MAX];
    char bad_path[IT_PATH_MAX];
    bool read_only;
    bool main_corrupt;
} ItStore;

bool it_store_init(ItStore* st, const ItStoreOps* ops, void* ctx, const char* main_path);
ItLoadStatus it_store_load(ItStore* st, ItSettings* out);
/* true on success. Saving is refused (false) when read_only. */
bool it_store_save(ItStore* st, const ItSettings* s);

/* ---- run state machine ---- */
typedef enum { ItPhaseWork, ItPhaseRest, ItPhaseDone } ItPhase;
typedef enum { ItEvtNone, ItEvtPhaseChanged, ItEvtFinished } ItEvt;

typedef struct {
    ItPhase phase;
    uint8_t round; /* 1-based */
    uint32_t remaining_ms;
} ItRun;

void it_run_start(ItRun* r, const ItSettings* s);
ItEvt it_run_advance(ItRun* r, const ItSettings* s, uint32_t elapsed_ms);
