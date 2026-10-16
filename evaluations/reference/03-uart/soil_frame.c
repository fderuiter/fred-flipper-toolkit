// Reviewer-side reference solution for FRED task 03. MIT licensed, original.
#include "soil_frame.h"
#include <string.h>

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t n) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < n; i++) {
        crc ^= (uint16_t)(data[i] << 8);
        for(int b = 0; b < 8; b++) crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

void soil_parser_reset(SoilParser* p) {
    memset(p, 0, sizeof(*p));
}

/* Returns true when the current frame failed and its buffered bytes must be rescanned. */
static bool step(SoilParser* p, uint8_t byte, SoilReadingCallback cb, void* ctx) {
    if(!p->in_frame) {
        if(byte == SOIL_SYNC) {
            p->in_frame = true;
            p->have = 0;
        }
        return false;
    }
    p->buf[p->have++] = byte;
    if(p->have == 1) {
        if(byte > SOIL_MAX_PAYLOAD) {
            p->stats.bad_length++;
            return true;
        }
        return false;
    }
    size_t len = p->buf[0];
    if(p->have < 1 + len + 2) return false;
    uint16_t want = (uint16_t)(p->buf[1 + len] | (p->buf[2 + len] << 8));
    if(soil_crc16_ccitt_false(p->buf, 1 + len) != want) {
        p->stats.bad_crc++;
        return true;
    }
    p->in_frame = false;
    p->have = 0;
    if(len < 4) {
        p->stats.bad_payload++;
        return false;
    }
    SoilReading r;
    r.moisture = (uint16_t)(p->buf[1] | (p->buf[2] << 8));
    r.temp_decicelsius = (int16_t)(uint16_t)(p->buf[3] | (p->buf[4] << 8));
    if(r.moisture > SOIL_MOISTURE_MAX) {
        p->stats.bad_payload++;
        return false;
    }
    p->stats.frames_ok++;
    if(cb) cb(&r, ctx);
    return false;
}

/* Iterative resync: after a failed frame, the buffered bytes (the consumed sync
 * byte excluded) are rescanned so a sync byte inside a corrupt frame can start
 * the next frame. Each failure drops at least the sync byte, so this terminates;
 * the work buffer never exceeds one frame plus one byte and no recursion is used. */
void soil_parser_feed(SoilParser* p, const uint8_t* data, size_t n, SoilReadingCallback cb, void* ctx) {
    uint8_t work[sizeof(p->buf) + 1];
    uint8_t tmp[sizeof(p->buf) + 1];
    for(size_t i = 0; i < n; i++) {
        size_t wn = 0, wi = 0;
        work[wn++] = data[i];
        while(wi < wn) {
            if(step(p, work[wi++], cb, ctx)) {
                size_t tn = p->have;
                memcpy(tmp, p->buf, tn);
                memcpy(tmp + tn, work + wi, wn - wi);
                tn += wn - wi;
                memcpy(work, tmp, tn);
                wn = tn;
                wi = 0;
                p->have = 0;
                p->in_frame = false;
                p->stats.resyncs++;
            }
        }
    }
}
