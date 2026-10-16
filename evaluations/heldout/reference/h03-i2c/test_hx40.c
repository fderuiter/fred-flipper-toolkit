// Host test for the H03 reference core: replays the synthetic transaction logs.
// Build: cc -std=c99 -Wall -Wextra -Werror hx40.c test_hx40.c
// Run:   ./a.out ../../fixtures/h03-i2c/captures
#include "hx40.h"
#include <stdio.h>
#include <string.h>

static int failures, checks;
#define CHECK(c)                                                                  \
    do {                                                                          \
        checks++;                                                                 \
        if(!(c)) {                                                                \
            failures++;                                                           \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                         \
    } while(0)

typedef struct {
    Hx40Policy p;
    Hx40Reading r[16];
    int nr;
    int skipped;
} Replay;

/* Each MEASURE line is one poll. Comment lines are data and are skipped. */
static int replay(const char* dir, const char* name, Replay* out) {
    char path[512], line[256];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE* f = fopen(path, "r");
    if(!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    memset(out, 0, sizeof(*out));
    hx40_policy_init(&out->p);
    while(fgets(line, sizeof line, f)) {
        if(line[0] == '#' || line[0] == '\n') continue;
        unsigned long t;
        char op[16], res[16];
        int used = 0;
        if(sscanf(line, "%lu %15s %15s%n", &t, op, res, &used) != 3) continue;
        if(strcmp(op, "MEASURE") == 0) {
            if(hx40_policy_before_poll(&out->p) == Hx40ActionSkipPoll) {
                out->skipped++;
                continue;
            }
            if(strcmp(res, "NACK") == 0) hx40_policy_on_event(&out->p, Hx40EventNack);
            else if(strcmp(res, "TIMEOUT") == 0) hx40_policy_on_event(&out->p, Hx40EventTimeout);
        } else if(strcmp(op, "READ") == 0 && strcmp(res, "ACK") == 0) {
            unsigned b[6];
            uint8_t fr[6];
            if(sscanf(line + used, "%x %x %x %x %x %x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
                continue;
            for(int i = 0; i < 6; i++) fr[i] = (uint8_t)b[i];
            Hx40Reading r;
            if(hx40_decode(fr, &r) == Hx40Ok) {
                hx40_policy_on_event(&out->p, Hx40EventReading);
                if(out->nr < 16) out->r[out->nr++] = r;
            } else {
                hx40_policy_on_event(&out->p, Hx40EventCrcError);
            }
        }
    }
    fclose(f);
    return 0;
}

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "../../fixtures/h03-i2c/captures";
    const uint8_t word[2] = {0xBE, 0xEF};
    CHECK(hx40_crc8(word, 2) == 0x92);

    Replay rp;
    CHECK(replay(dir, "normal_log.txt", &rp) == 0);
    const int32_t want[4][2] = {{4500, 2500}, {4531, 2507}, {4551, 2515}, {4570, 2524}};
    CHECK(rp.nr == 4);
    for(int i = 0; i < 4 && i < rp.nr; i++) CHECK(rp.r[i].rh_centi == want[i][0] && rp.r[i].t_centi == want[i][1]);
    CHECK(rp.p.nacks == 1 && rp.p.recoveries == 0 && rp.p.crc_errors == 0 && !rp.p.stuck);

    CHECK(replay(dir, "stuck_bus_log.txt", &rp) == 0);
    CHECK(rp.nr == 2);
    for(int i = 0; i < rp.nr; i++) CHECK(rp.r[i].rh_centi == 3750 && rp.r[i].t_centi == 2063);
    CHECK(rp.p.crc_errors == 1 && rp.p.timeouts == 3 && rp.p.recoveries >= 1);
    CHECK(!rp.p.stuck && rp.p.consecutive_recoveries == 0);

    CHECK(replay(dir, "dead_bus_log.txt", &rp) == 0);
    CHECK(rp.nr == 0 && rp.p.stuck);
    CHECK(rp.p.recoveries <= HX40_MAX_RECOVERIES + 1);
    CHECK(rp.skipped > 0);

    /* long dead bus: recovery attempts grow only once per backoff window */
    Hx40Policy p;
    hx40_policy_init(&p);
    for(int i = 0; i < 1000; i++)
        if(hx40_policy_before_poll(&p) != Hx40ActionSkipPoll) hx40_policy_on_event(&p, Hx40EventTimeout);
    CHECK(p.stuck && p.recoveries <= HX40_MAX_RECOVERIES + 1000 / HX40_STUCK_RETRY_POLLS + 1);

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
