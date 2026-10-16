#include "soil_parser.h"

enum {
    StateSync,
    StateLength,
    StateBody, // payload + 2 CRC bytes
};

uint16_t soil_crc16_ccitt_false(const uint8_t* data, size_t len) {
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
    p->state = StateSync;
    p->len = 0;
    p->pos = 0;
}

/* On a failed frame, the bytes after the false sync byte may contain a real frame
 * start. Replay them (bounded: at most 1+32+2 bytes, each replay strictly shorter). */
static SoilParseResult
    soil_parser_rescan(SoilParser* p, const uint8_t* bytes, size_t count, SoilReading* out) {
    SoilParseResult result = SoilParseError;
    soil_parser_reset(p);
    for(size_t i = 0; i < count; i++) {
        SoilParseResult r = soil_parser_feed(p, bytes[i], out);
        if(r == SoilParseFrame) result = SoilParseFrame;
    }
    return result;
}

SoilParseResult soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* out) {
    switch(p->state) {
    case StateSync:
        if(byte == SOIL_SYNC) p->state = StateLength;
        return SoilParseNone;

    case StateLength:
        if(byte > SOIL_MAX_PAYLOAD) {
            // Invalid length: this was not a frame start. The byte may itself be a sync.
            soil_parser_reset(p);
            if(byte == SOIL_SYNC) p->state = StateLength;
            return SoilParseError;
        }
        p->len = byte;
        p->buf[0] = byte;
        p->pos = 1;
        p->state = StateBody;
        return SoilParseNone;

    default: // StateBody
        p->buf[p->pos++] = byte;
        if(p->pos < (uint8_t)(1 + p->len + 2)) return SoilParseNone;

        {
            uint8_t total = p->pos;
            uint16_t calc = soil_crc16_ccitt_false(p->buf, 1 + p->len);
            uint16_t got = (uint16_t)(p->buf[1 + p->len] | (p->buf[2 + p->len] << 8));
            bool ok = (calc == got) && p->len >= SOIL_MIN_PAYLOAD;
            if(ok) {
                uint16_t m = (uint16_t)(p->buf[1] | (p->buf[2] << 8));
                int16_t t = (int16_t)(p->buf[3] | (p->buf[4] << 8));
                if(m > 1000) ok = false;
                else {
                    out->moisture = m;
                    out->temperature_dc = t;
                }
            }
            if(ok) {
                soil_parser_reset(p);
                return SoilParseFrame;
            }
            // Rejected: rescan everything after the sync byte (already in buf).
            uint8_t copy[sizeof(p->buf)];
            for(uint8_t i = 0; i < total; i++) copy[i] = p->buf[i];
            SoilParseResult r = soil_parser_rescan(p, copy, total, out);
            return r == SoilParseFrame ? SoilParseFrame : SoilParseError;
        }
    }
}
