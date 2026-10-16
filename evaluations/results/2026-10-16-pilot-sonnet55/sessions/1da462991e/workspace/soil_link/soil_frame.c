#include "soil_frame.h"

#include <string.h>

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

void soil_parser_reset(SoilParser* p) {
    memset(p, 0, sizeof(*p));
}

static void drop(SoilParser* p, size_t n) {
    p->len -= n;
    memmove(p->buf, p->buf + n, p->len);
}

static void count_error(SoilParser* p) {
    p->error_count++;
    p->resyncing = true;
}

bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out) {
    bool got = false;

    // len is always < SOIL_MAX_FRAME here: complete or invalid frames are consumed below.
    p->buf[p->len++] = byte;

    while(p->len > 0) {
        if(p->buf[0] != SOIL_SYNC) {
            // Garbage between frames: one error per stretch, not per byte.
            if(!p->resyncing) count_error(p);
            drop(p, 1);
            continue;
        }
        if(p->len < 2) break;

        uint8_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) {
            count_error(p);
            drop(p, 1); // the 0xA5 may have been noise; rescan from the next byte
            continue;
        }

        size_t total = 2 + (size_t)n + 2;
        if(p->len < total) break; // partial frame (split read): wait for more bytes

        uint16_t rx = (uint16_t)(p->buf[2 + n] | (p->buf[3 + n] << 8));
        if(soil_crc16(p->buf + 1, 1 + (size_t)n) != rx) {
            count_error(p);
            drop(p, 1); // a real frame may start inside this one
            continue;
        }

        uint16_t moisture = (uint16_t)(p->buf[2] | (p->buf[3] << 8));
        if(n < SOIL_MIN_PAYLOAD || moisture > 1000) {
            // CRC ok but contents unusable; frame boundaries are trustworthy, skip it whole.
            count_error(p);
            drop(p, total);
            continue;
        }

        if(out) {
            out->moisture = moisture;
            out->temperature_dc = (int16_t)(p->buf[4] | (p->buf[5] << 8));
        }
        p->frame_count++;
        p->resyncing = false;
        got = true;
        drop(p, total);
        // Only one byte was added, so at most one frame can complete per call.
        break;
    }
    return got;
}
