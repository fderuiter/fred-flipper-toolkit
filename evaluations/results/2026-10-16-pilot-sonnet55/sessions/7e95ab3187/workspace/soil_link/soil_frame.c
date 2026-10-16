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

static void drop_front(SoilParser* p, uint8_t n) {
    memmove(p->buf, p->buf + n, p->len - n);
    p->len -= n;
}

bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out) {
    bool produced = false;

    if(p->len == 0 && byte != SOIL_SYNC) {
        // Garbage between frames: one error per run, not per byte.
        if(!p->in_garbage) {
            p->in_garbage = true;
            p->error_count++;
        }
        return false;
    }
    p->in_garbage = false;
    p->buf[p->len++] = byte;

    // Re-scan after any failure: a false sync byte must not swallow a real frame
    // that starts inside the bytes already buffered.
    while(p->len > 0) {
        if(p->buf[0] != SOIL_SYNC) {
            drop_front(p, 1);
            continue;
        }
        if(p->len < 2) break;
        uint8_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) {
            p->error_count++;
            drop_front(p, 1);
            continue;
        }
        uint8_t total = (uint8_t)(n + 4);
        if(p->len < total) break; // need more bytes

        uint16_t rx_crc = (uint16_t)(p->buf[2 + n] | (p->buf[3 + n] << 8)); // little endian
        uint16_t calc = soil_crc16(&p->buf[1], (size_t)n + 1);
        if(calc != rx_crc || n < SOIL_MIN_PAYLOAD) {
            p->error_count++;
            drop_front(p, 1);
            continue;
        }
        uint16_t moisture = (uint16_t)(p->buf[2] | (p->buf[3] << 8));
        int16_t temp = (int16_t)(uint16_t)(p->buf[4] | (p->buf[5] << 8));
        if(moisture > 1000) {
            p->error_count++;
            drop_front(p, total); // CRC-valid frame with invalid value: skip whole frame
            continue;
        }
        out->moisture = moisture;
        out->temperature_dc = temp;
        p->frame_count++;
        produced = true;
        drop_front(p, total);
        break; // at most one frame per byte; remaining bytes (if any) are rescanned next feed
    }
    return produced;
}
