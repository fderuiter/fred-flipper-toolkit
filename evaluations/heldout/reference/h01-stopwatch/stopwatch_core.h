// Reviewer-side reference solution for FRED held-out task H01. MIT licensed, original.
// Pure C99 stopwatch state: elapsed time from a monotonic millisecond clock
// supplied by the caller, a fixed ring of laps, and mm:ss.cc formatting.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STOPWATCH_LAPS 10u

typedef struct {
    bool running;
    uint32_t start_ms; /* clock value when the current run started */
    uint32_t banked_ms; /* elapsed time of earlier runs */
    uint32_t laps[STOPWATCH_LAPS]; /* ring, total elapsed at lap time */
    uint32_t lap_head; /* next slot */
    uint32_t lap_count; /* laps recorded, saturates at STOPWATCH_LAPS */
} Stopwatch;

void stopwatch_reset(Stopwatch* sw);
void stopwatch_toggle(Stopwatch* sw, uint32_t now_ms);
/* Records a lap only while running; returns false otherwise. */
bool stopwatch_lap(Stopwatch* sw, uint32_t now_ms);
/* Resets only while stopped; returns false otherwise. */
bool stopwatch_long_ok(Stopwatch* sw);
/* Unsigned subtraction keeps this correct across one clock wrap. */
uint32_t stopwatch_elapsed(const Stopwatch* sw, uint32_t now_ms);
/* i = 0 is the newest lap. Returns false if i >= lap_count. */
bool stopwatch_lap_get(const Stopwatch* sw, uint32_t i, uint32_t* out_ms);
/* Writes "mm:ss.cc"; saturates at 99:59.99. Returns false if cap < 9. */
bool stopwatch_format(uint32_t ms, char* buf, size_t cap);
