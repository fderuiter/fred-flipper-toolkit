// Host test: gcc -Wall -Wextra -o test_logic test/test_logic.c && ./test_logic
#include <assert.h>
#include <stdio.h>
#include "../tally_logic.h"

int main(void) {
    assert(tally_decrement(0) == 0);
    assert(tally_decrement(1) == 0);
    assert(tally_increment(0) == 1);
    assert(tally_increment(TALLY_MAX) == TALLY_MAX);
    assert(tally_decrement(TALLY_MAX) == TALLY_MAX - 1);
    assert(tally_reset() == 0);
    puts("ok");
    return 0;
}
