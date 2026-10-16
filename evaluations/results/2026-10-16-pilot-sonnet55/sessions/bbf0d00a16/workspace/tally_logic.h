#pragma once
#include <stdint.h>

// Pure counter logic, kept free of Furi so it can be host-tested.
#define TALLY_MAX 999999U

static inline uint32_t tally_inc(uint32_t count) {
    return (count < TALLY_MAX) ? count + 1U : TALLY_MAX;
}

static inline uint32_t tally_dec(uint32_t count) {
    return (count > 0U) ? count - 1U : 0U;
}
