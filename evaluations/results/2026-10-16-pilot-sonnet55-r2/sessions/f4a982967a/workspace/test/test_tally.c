#include "../tally_counter/tally.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(tally_dec(0) == 0);
    assert(tally_dec(1) == 0);
    assert(tally_inc(0) == 1);
    assert(tally_inc(TALLY_MAX) == TALLY_MAX);
    assert(tally_inc(TALLY_MAX - 1) == TALLY_MAX);
    uint32_t v = 0;
    for(int i = 0; i < 5; i++) v = tally_inc(v);
    for(int i = 0; i < 8; i++) v = tally_dec(v);
    assert(v == 0);
    puts("ok");
    return 0;
}
