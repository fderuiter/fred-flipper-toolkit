#include "soil_parser.h"

#include <string.h>

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t size) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < size; i++) {
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

static void drop_front(SoilParser* p, size_t count) {
    memmove(p->buf, p->buf + count, p->len - count);
    p->len -= count;
}

// Decode as many complete frames as the buffer holds. A failed candidate frame
// drops only its sync byte and rescans, so a real frame hidden inside the bytes
// of a corrupt one is not lost.
static size_t process(SoilParser* p) {
    size_t decoded = 0;

    while(p->len > 0) {
        if(p->buf[0] != SOIL_SYNC) {
            if(!p->in_garbage) {
                p->in_garbage = true;
                p->errors++;
            }
            drop_front(p, 1);
            continue;
        }
        p->in_garbage = false;

        if(p->len < 2) break;

        const size_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) {
            p->errors++;
            drop_front(p, 1);
            p->in_garbage = true; // already counted this error
            continue;
        }

        const size_t frame_len = 2 + n + 2;
        if(p->len < frame_len) break;

        const uint16_t expected = soil_crc16_ccitt_false(p->buf + 1, 1 + n);
        const uint16_t received = (uint16_t)(p->buf[2 + n] | (p->buf[3 + n] << 8));
        if(expected != received) {
            p->errors++;
            drop_front(p, 1);
            p->in_garbage = true;
            continue;
        }

        if(n < SOIL_MIN_PAYLOAD) {
            // CRC-valid but too short to hold a reading.
            p->errors++;
        } else {
            const uint8_t* pl = p->buf + 2;
            p->moisture = (uint16_t)(pl[0] | (pl[1] << 8));
            p->temperature_dc = (int16_t)(pl[2] | (pl[3] << 8));
            p->have_reading = true;
            p->frames_ok++;
            decoded++;
        }
        drop_front(p, frame_len);
    }

    return decoded;
}

size_t soil_parser_feed(SoilParser* p, const uint8_t* data, size_t size) {
    size_t decoded = 0;
    for(size_t i = 0; i < size; i++) {
        p->buf[p->len++] = data[i];
        // process() leaves fewer than a full frame buffered, so buf cannot overflow.
        decoded += process(p);
    }
    return decoded;
}
