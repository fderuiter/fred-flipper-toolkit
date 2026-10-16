#pragma once
#include <stdint.h>

// Pure counter logic, no Furi dependencies so it can be host-tested.
// Capped at 6 digits so the count always fits the 128 px wide screen in FontBigNumbers.
#define TALLY_MAX 999999U

static inline uint32_t tally_inc(uint32_t v) {
    return v < TALLY_MAX ? v + 1 : TALLY_MAX;
}

static inline uint32_t tally_dec(uint32_t v) {
    return v > 0 ? v - 1 : 0;
}
