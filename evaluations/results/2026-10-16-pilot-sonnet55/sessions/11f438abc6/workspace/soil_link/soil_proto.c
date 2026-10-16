#include "soil_proto.h"
#include <string.h>

void soil_parser_reset(SoilParser* p) {
    memset(p, 0, sizeof(*p));
}

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout
uint16_t soil_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    while(len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for(int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void drop(SoilParser* p, size_t n) {
    p->len -= n;
    memmove(p->buf, p->buf + n, p->len);
}

bool soil_parser_feed(SoilParser* p, uint8_t byte) {
    bool got = false;
    // The loop below always consumes until fewer than a full frame is buffered,
    // so len < SOIL_BUF_SIZE here.
    p->buf[p->len++] = byte;

    // Re-scan after every byte so a failed frame resyncs on any sync byte
    // already buffered (bursts, and sync bytes inside a bad frame).
    while(p->len > 0) {
        if(p->buf[0] != SOIL_SYNC) {
            if(!p->in_garbage) {
                p->in_garbage = true;
                p->errors++;
            }
            drop(p, 1);
            continue;
        }
        p->in_garbage = false;

        if(p->len < 2) break;
        uint8_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) {
            p->errors++;
            drop(p, 1);
            continue;
        }
        size_t total = 2 + (size_t)n + 2;
        if(p->len < total) break;

        uint16_t rx = p->buf[2 + n] | ((uint16_t)p->buf[3 + n] << 8);
        if(soil_crc16(p->buf + 1, 1 + n) != rx || n < SOIL_MIN_PAYLOAD) {
            p->errors++;
            drop(p, 1);
            continue;
        }
        uint16_t m = p->buf[2] | ((uint16_t)p->buf[3] << 8);
        if(m > 1000) {
            p->errors++; // CRC ok but out of range
            drop(p, total);
            continue;
        }
        p->moisture = m;
        p->temp_dc = (int16_t)(p->buf[4] | ((uint16_t)p->buf[5] << 8));
        p->have_reading = true;
        p->frames++;
        got = true;
        drop(p, total);
    }
    return got;
}
