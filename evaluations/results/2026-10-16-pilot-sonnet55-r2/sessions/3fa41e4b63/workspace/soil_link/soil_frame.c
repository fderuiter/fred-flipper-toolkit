#include "soil_frame.h"

#include <string.h>

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for(int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void soil_parser_reset(SoilParser* p) {
    memset(p, 0, sizeof(*p));
}

typedef enum {
    EvalNeedMore,
    EvalComplete,
    EvalBad,
} EvalResult;

static EvalResult soil_eval(const SoilParser* p) {
    if(p->fill < 2) return EvalNeedMore;
    uint8_t n = p->buf[1];
    if(n > SOIL_MAX_PAYLOAD) return EvalBad;
    if(p->fill < (uint8_t)(4 + n)) return EvalNeedMore;
    uint16_t rx = (uint16_t)(p->buf[2 + n] | (p->buf[3 + n] << 8));
    return soil_crc16_ccitt_false(&p->buf[1], (size_t)n + 1) == rx ? EvalComplete : EvalBad;
}

// Drop the leading byte and shift to the next sync byte in what is buffered.
static void soil_resync(SoilParser* p) {
    uint8_t i = 1;
    while(i < p->fill && p->buf[i] != SOIL_SYNC_BYTE) i++;
    p->skipped_bytes += i;
    memmove(p->buf, &p->buf[i], p->fill - i);
    p->fill -= i;
}

bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out) {
    if(p->fill == 0) {
        if(byte != SOIL_SYNC_BYTE) {
            p->skipped_bytes++;
            return false;
        }
    }
    p->buf[p->fill++] = byte; // fill < SOIL_FRAME_MAX always holds: eval consumes at 4+N <= max

    for(;;) {
        EvalResult r = soil_eval(p);
        if(r == EvalNeedMore) return false;
        if(r == EvalBad) {
            p->errors++;
            soil_resync(p); // re-evaluate remaining bytes, which may hold a full frame
            continue;
        }
        // Complete and CRC-valid.
        uint8_t n = p->buf[1];
        bool ok = false;
        if(n >= SOIL_MIN_PAYLOAD) {
            uint16_t m = (uint16_t)(p->buf[2] | (p->buf[3] << 8));
            if(m <= SOIL_MAX_MOISTURE) {
                out->moisture = m;
                out->temperature_dc = (int16_t)(p->buf[4] | (p->buf[5] << 8));
                p->frames_ok++;
                ok = true;
            }
        }
        if(!ok) p->errors++;
        p->fill = 0;
        return ok;
    }
}
