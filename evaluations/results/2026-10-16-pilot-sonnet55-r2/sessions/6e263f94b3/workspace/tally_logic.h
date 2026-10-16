#pragma once
#include <stdint.h>

#define TALLY_MAX 999999U

static inline uint32_t tally_increment(uint32_t count) {
    return (count >= TALLY_MAX) ? TALLY_MAX : count + 1;
}

static inline uint32_t tally_decrement(uint32_t count) {
    return (count == 0) ? 0 : count - 1;
}
