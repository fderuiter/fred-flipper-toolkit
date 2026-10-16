#include <assert.h>
#include "tally_logic.h"

int main(void) {
    assert(tally_decrement(0) == 0);
    assert(tally_decrement(5) == 4);
    assert(tally_increment(0) == 1);
    assert(tally_increment(TALLY_MAX) == TALLY_MAX);
    return 0;
}
