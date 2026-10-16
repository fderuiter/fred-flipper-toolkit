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

static void soil_drop(SoilParser* p, uint8_t count) {
    memmove(p->buf, p->buf + count, p->len - count);
    p->len -= count;
}

bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out) {
    bool got = false;

    // len < SOIL_MAX_FRAME is an invariant: a full frame is always consumed or dropped below.
    p->buf[p->len++] = byte;

    // Rescan loop: after a failure, drop only the sync byte and re-examine the rest, so a real
    // frame that started inside a corrupt one is not lost.
    while(p->len > 0) {
        if(p->buf[0] != SOIL_SYNC) {
            uint8_t skip = 1;
            while(skip < p->len && p->buf[skip] != SOIL_SYNC) skip++;
            if(!p->in_garbage) {
                p->in_garbage = true;
                p->error_count++;
            }
            soil_drop(p, skip);
            continue;
        }
        p->in_garbage = false;

        if(p->len < 2) break;
        uint8_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) {
            p->error_count++;
            soil_drop(p, 1);
            continue;
        }

        size_t total = 2u + n + 2u;
        if(p->len < total) break;

        uint16_t want = (uint16_t)(p->buf[2 + n] | ((uint16_t)p->buf[3 + n] << 8));
        if(soil_crc16(p->buf + 1, 1u + n) != want) {
            p->error_count++;
            soil_drop(p, 1);
            continue;
        }

        uint16_t moisture = (uint16_t)(p->buf[2] | ((uint16_t)p->buf[3] << 8));
        if(n < SOIL_MIN_PAYLOAD || moisture > SOIL_MOISTURE_MAX) {
            // CRC is good but content is unusable: consume the frame, count it.
            p->error_count++;
            soil_drop(p, (uint8_t)total);
            continue;
        }

        out->moisture = moisture;
        out->temperature_dc = (int16_t)(p->buf[4] | ((uint16_t)p->buf[5] << 8));
        p->frame_count++;
        got = true;
        soil_drop(p, (uint8_t)total);
        // Keep looping: leftover bytes belong to the next frame. Only the last reading in
        // a feed is reported, but one byte completes at most one frame, so none are lost.
    }
    return got;
}
