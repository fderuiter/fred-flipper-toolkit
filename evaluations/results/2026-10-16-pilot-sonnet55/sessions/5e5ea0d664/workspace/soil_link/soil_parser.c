#include "soil_parser.h"
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

static void drop_front(SoilParser* p, size_t n) {
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
}

bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out) {
    bool got = false;
    p->buf[p->len++] = byte; // len < SOIL_MAX_FRAME is guaranteed by the loop below

    for(;;) {
        // Resync: discard bytes until a sync byte is at the front.
        size_t skip = 0;
        while(skip < p->len && p->buf[skip] != SOIL_SYNC) skip++;
        if(skip) {
            if(!p->in_garbage) {
                p->error_count++;
                p->in_garbage = true;
            }
            drop_front(p, skip);
        }
        if(p->len < 2) break;

        uint8_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) { // bad length: this A5 was not a real sync
            p->error_count++;
            drop_front(p, 1);
            continue;
        }
        size_t total = 4 + (size_t)n;
        if(p->len < total) break; // incomplete, wait for more bytes (handles split reads)

        uint16_t rx = (uint16_t)(p->buf[2 + n] | (p->buf[3 + n] << 8));
        if(soil_crc16(p->buf + 1, 1 + n) != rx) {
            p->error_count++;
            drop_front(p, 1); // rescan: a real frame may start inside this one
            continue;
        }

        p->in_garbage = false;
        if(n >= 4) {
            uint16_t m = (uint16_t)(p->buf[2] | (p->buf[3] << 8));
            if(m <= 1000) {
                out->moisture = m;
                out->temperature_dc = (int16_t)(p->buf[4] | (p->buf[5] << 8));
                got = true;
            } else {
                p->error_count++; // CRC ok but value out of range
            }
        } else {
            p->error_count++; // payload too short for moisture + temperature
        }
        drop_front(p, total);
    } // if several frames complete in one call, *out keeps the latest
    return got;
}
