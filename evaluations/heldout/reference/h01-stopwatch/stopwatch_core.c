// Reviewer-side reference solution for FRED held-out task H01. MIT licensed, original.
#include "stopwatch_core.h"
#include <string.h>

#define MAX_SHOWN_MS (99u * 60000u + 59u * 1000u + 990u)

void stopwatch_reset(Stopwatch* sw) {
    memset(sw, 0, sizeof(*sw));
}

uint32_t stopwatch_elapsed(const Stopwatch* sw, uint32_t now_ms) {
    uint32_t total = sw->banked_ms;
    if(sw->running) {
        uint32_t run = now_ms - sw->start_ms; /* wrap-safe */
        total = (UINT32_MAX - total < run) ? UINT32_MAX : total + run;
    }
    return total;
}

void stopwatch_toggle(Stopwatch* sw, uint32_t now_ms) {
    if(sw->running) {
        sw->banked_ms = stopwatch_elapsed(sw, now_ms);
        sw->running = false;
    } else {
        sw->start_ms = now_ms;
        sw->running = true;
    }
}

bool stopwatch_lap(Stopwatch* sw, uint32_t now_ms) {
    if(!sw->running) return false;
    sw->laps[sw->lap_head] = stopwatch_elapsed(sw, now_ms);
    sw->lap_head = (sw->lap_head + 1u) % STOPWATCH_LAPS;
    if(sw->lap_count < STOPWATCH_LAPS) sw->lap_count++;
    return true;
}

bool stopwatch_long_ok(Stopwatch* sw) {
    if(sw->running) return false;
    stopwatch_reset(sw);
    return true;
}

bool stopwatch_lap_get(const Stopwatch* sw, uint32_t i, uint32_t* out_ms) {
    if(i >= sw->lap_count) return false;
    uint32_t idx = (sw->lap_head + STOPWATCH_LAPS - 1u - i) % STOPWATCH_LAPS;
    *out_ms = sw->laps[idx];
    return true;
}

bool stopwatch_format(uint32_t ms, char* buf, size_t cap) {
    if(cap < 9) return false;
    if(ms > MAX_SHOWN_MS) ms = MAX_SHOWN_MS;
    uint32_t cs = (ms / 10u) % 100u;
    uint32_t s = (ms / 1000u) % 60u;
    uint32_t m = ms / 60000u;
    buf[0] = (char)('0' + m / 10u);
    buf[1] = (char)('0' + m % 10u);
    buf[2] = ':';
    buf[3] = (char)('0' + s / 10u);
    buf[4] = (char)('0' + s % 10u);
    buf[5] = '.';
    buf[6] = (char)('0' + cs / 10u);
    buf[7] = (char)('0' + cs % 10u);
    buf[8] = '\0';
    return true;
}
