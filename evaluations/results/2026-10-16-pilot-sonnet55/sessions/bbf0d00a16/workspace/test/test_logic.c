#include <assert.h>
#include <stdio.h>
#include "../tally_logic.h"

int main(void) {
    assert(tally_dec(0) == 0);
    assert(tally_dec(1) == 0);
    assert(tally_inc(0) == 1);
    assert(tally_inc(TALLY_MAX) == TALLY_MAX);
    assert(tally_dec(TALLY_MAX) == TALLY_MAX - 1);
    puts("ok");
    return 0;
}
