#include "soil_proto.h"

#include <string.h>

void soil_parser_reset(SoilParser* p) {
    p->count = 0;
}

uint16_t soil_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for(size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for(int bit = 0; bit < 8; bit++) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Process one byte against the current candidate. Returns true if the candidate was rejected
 * (error already counted); p->buf/p->count still hold the rejected bytes for re-scanning. */
static bool soil_step(SoilParser* p, uint8_t byte, SoilEvents* ev) {
    if(p->count == 0) {
        if(byte == SOIL_SYNC_BYTE) p->buf[p->count++] = byte;
        return false;
    }

    p->buf[p->count++] = byte;

    if(p->count == 2) {
        if(byte > SOIL_MAX_PAYLOAD) {
            ev->errors++;
            return true;
        }
        return false;
    }

    const uint8_t total = (uint8_t)(2 + p->buf[1] + 2);
    if(p->count < total) return false;

    /* Complete candidate. CRC covers length + payload, transmitted little endian. */
    const uint8_t n = p->buf[1];
    const uint16_t got = (uint16_t)(p->buf[2 + n] | ((uint16_t)p->buf[3 + n] << 8));
    const uint16_t want = soil_crc16(&p->buf[1], (size_t)n + 1);
    if(got != want || n < SOIL_MIN_PAYLOAD) {
        ev->errors++;
        return true;
    }

    const uint16_t moisture = (uint16_t)(p->buf[2] | ((uint16_t)p->buf[3] << 8));
    if(moisture > SOIL_MOISTURE_MAX) {
        ev->errors++;
        /* CRC was valid, so this frame boundary is trustworthy: drop it whole, no re-scan. */
        p->count = 0;
        return false;
    }

    ev->has_reading = true;
    ev->reading.moisture = moisture;
    ev->reading.temperature_dc = (int16_t)(p->buf[4] | ((uint16_t)p->buf[5] << 8));
    ev->frames_ok++;
    p->count = 0;
    return false;
}

void soil_parser_feed(SoilParser* p, uint8_t byte, SoilEvents* ev) {
    if(!soil_step(p, byte, ev)) return;

    /* Rejected: re-scan everything after the old sync byte. Each pass strictly shortens the
     * pending bytes, so this terminates; the first rejection was already counted, later ones
     * count their own candidates. */
    uint8_t pending[SOIL_MAX_FRAME];
    uint8_t pending_len = (uint8_t)(p->count - 1);
    memcpy(pending, &p->buf[1], pending_len);
    p->count = 0;

    uint8_t i = 0;
    while(i < pending_len) {
        if(soil_step(p, pending[i++], ev)) {
            /* Candidate rebuilt from replayed bytes was rejected: replay its tail, then the
             * bytes of `pending` not yet consumed. */
            uint8_t tail[SOIL_MAX_FRAME];
            uint8_t tail_len = (uint8_t)(p->count - 1);
            memcpy(tail, &p->buf[1], tail_len);
            const uint8_t rest = (uint8_t)(pending_len - i);
            /* tail is a suffix of already-consumed pending bytes, so tail_len + rest <= the
             * previous pending_len and fits in the buffer. */
            memmove(&pending[tail_len], &pending[i], rest);
            memcpy(pending, tail, tail_len);
            pending_len = (uint8_t)(tail_len + rest);
            p->count = 0;
            i = 0;
        }
    }
}

void soil_parser_timeout(SoilParser* p, SoilEvents* ev) {
    if(p->count == 0) return;
    ev->errors++;
    p->count = 0;
}
