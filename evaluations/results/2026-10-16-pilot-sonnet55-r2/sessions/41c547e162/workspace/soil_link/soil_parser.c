#include "soil_parser.h"

#include <string.h>

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    while(len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for(int i = 0; i < 8; i++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

void soil_parser_reset(SoilParser* p) {
    memset(p, 0, sizeof(*p));
}

static void drop_first(SoilParser* p) {
    memmove(p->buf, p->buf + 1, --p->len);
}

// Count one error per run of bad data, not per byte.
static void note_garbage(SoilParser* p) {
    if(!p->in_garbage) {
        p->errors++;
        p->in_garbage = true;
    }
}

bool soil_parser_feed(SoilParser* p, uint8_t byte) {
    bool got_frame = false;
    p->buf[p->len++] = byte; // len never exceeds SOIL_MAX_FRAME, see below

    // Re-scan after every resync: a false sync byte inside garbage or a bad
    // frame may hide a real frame that is already in the buffer.
    while(p->len > 0) {
        if(p->buf[0] != SOIL_SYNC) {
            note_garbage(p);
            drop_first(p);
            continue;
        }
        if(p->len < 2) break;

        size_t n = p->buf[1];
        if(n > SOIL_MAX_PAYLOAD) {
            note_garbage(p);
            drop_first(p);
            continue;
        }

        size_t total = 2 + n + 2;
        if(p->len < total) break; // wait for the rest (split reads)

        uint16_t rx_crc = (uint16_t)(p->buf[2 + n] | (p->buf[3 + n] << 8));
        if(soil_crc16_ccitt_false(p->buf + 1, 1 + n) != rx_crc) {
            note_garbage(p);
            drop_first(p);
            continue;
        }

        const uint8_t* pl = p->buf + 2;
        uint16_t moisture = n >= 4 ? (uint16_t)(pl[0] | (pl[1] << 8)) : 0xFFFF;
        if(n < 4 || moisture > 1000) {
            // CRC fine but contents unusable: consume the frame, count it.
            p->errors++;
        } else {
            p->moisture = moisture;
            p->temperature = (int16_t)(pl[2] | (pl[3] << 8));
            p->has_reading = true;
            p->frames_ok++;
            got_frame = true;
        }
        p->in_garbage = false;
        memmove(p->buf, p->buf + total, p->len - total);
        p->len -= total;
    }
    return got_frame;
}
