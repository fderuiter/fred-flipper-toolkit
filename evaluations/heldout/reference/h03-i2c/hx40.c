// Reviewer-side reference solution for FRED held-out task H03. MIT licensed, original.
#include "hx40.h"
#include <string.h>

uint8_t hx40_crc8(const uint8_t* data, uint32_t len) {
    uint8_t crc = 0xFF;
    for(uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for(int b = 0; b < 8; b++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
    }
    return crc;
}

Hx40Decode hx40_decode(const uint8_t f[6], Hx40Reading* out) {
    if(hx40_crc8(f, 2) != f[2] || hx40_crc8(f + 3, 2) != f[5]) return Hx40CrcError;
    uint32_t rh = ((uint32_t)f[0] << 8) | f[1];
    uint32_t t = ((uint32_t)f[3] << 8) | f[4];
    /* rounded fixed point, no float needed */
    out->rh_centi = (int32_t)((10000u * rh + 32767u) / 65535u);
    out->t_centi = -4500 + (int32_t)((17500u * t + 32767u) / 65535u);
    return Hx40Ok;
}

void hx40_policy_init(Hx40Policy* p) {
    memset(p, 0, sizeof(*p));
}

Hx40Action hx40_policy_before_poll(Hx40Policy* p) {
    if(!p->stuck) return Hx40ActionNone;
    if(++p->stuck_polls < HX40_STUCK_RETRY_POLLS) return Hx40ActionSkipPoll;
    p->stuck_polls = 0;
    return Hx40ActionNone; /* one probe; a timeout below triggers one recovery */
}

Hx40Action hx40_policy_on_event(Hx40Policy* p, Hx40Event e) {
    switch(e) {
    case Hx40EventReading:
        p->readings++;
        p->consecutive_timeouts = 0;
        p->consecutive_recoveries = 0;
        p->stuck = false;
        p->stuck_polls = 0;
        return Hx40ActionNone;
    case Hx40EventCrcError:
        p->crc_errors++;
        p->consecutive_timeouts = 0;
        return Hx40ActionNone;
    case Hx40EventNack:
        p->nacks++;
        return Hx40ActionNone;
    case Hx40EventTimeout:
        p->timeouts++;
        p->consecutive_timeouts++;
        if(p->stuck) { /* backoff probe failed: one recovery attempt, stay stuck */
            p->recoveries++;
            return Hx40ActionRecover;
        }
        if(p->consecutive_timeouts < HX40_TIMEOUTS_BEFORE_RECOVERY) return Hx40ActionNone;
        if(p->consecutive_recoveries >= HX40_MAX_RECOVERIES) {
            p->stuck = true;
            p->stuck_polls = 0;
            return Hx40ActionNone;
        }
        p->consecutive_recoveries++;
        p->recoveries++;
        return Hx40ActionRecover;
    }
    return Hx40ActionNone;
}
