#include "soil_frame.h"

#include <string.h>

void soil_parser_reset(SoilParser* p) {
    memset(p, 0, sizeof(*p));
}

uint16_t soil_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for(int b = 0; b < 8; b++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void soil_drop(SoilParser* p, size_t count) {
    if(count >= p->len) {
        p->len = 0;
    } else {
        memmove(p->buf, p->buf + count, p->len - count);
        p->len -= count;
    }
}

bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out) {
    // Invariant: len < SOIL_MAX_FRAME_SIZE on entry, because a complete
    // candidate is always consumed or rejected before returning.
    p->buf[p->len++] = byte;

    // Every pass either returns or drops at least one byte, so the work per
    // call is bounded by SOIL_MAX_FRAME_SIZE passes.
    while(p->len > 0) {
        if(p->buf[0] != SOIL_SYNC_BYTE) {
            if(!p->in_garbage) {
                p->in_garbage = true;
                p->error_count++; // one error per run of garbage
            }
            soil_drop(p, 1);
            continue;
        }
        p->in_garbage = false;
        if(p->len < 2) return false;

        size_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) {
            p->error_count++;
            soil_drop(p, 1);
            continue;
        }
        if(p->len < 4 + n) return false;

        uint16_t rx_crc = (uint16_t)(p->buf[2 + n] | (p->buf[3 + n] << 8));
        if(soil_crc16(p->buf + 1, 1 + n) != rx_crc) {
            p->error_count++;
            soil_drop(p, 1);
            continue;
        }

        // CRC is valid; the payload must still be usable.
        bool ok = false;
        if(n >= SOIL_MIN_PAYLOAD) {
            uint16_t moisture = (uint16_t)(p->buf[2] | (p->buf[3] << 8));
            if(moisture <= SOIL_MOISTURE_MAX) {
                out->moisture = moisture;
                out->temperature_dc = (int16_t)(uint16_t)(p->buf[4] | (p->buf[5] << 8));
                ok = true;
            }
        }
        soil_drop(p, 4 + n);
        if(ok) {
            p->frame_count++;
            return true;
        }
        p->error_count++;
    }
    return false;
}
