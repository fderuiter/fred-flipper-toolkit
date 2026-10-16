#include "soil_frame.h"

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
    p->state = SoilStateSync;
}

static bool soil_try_frame(SoilParser* p, uint16_t rx_crc, SoilReading* reading) {
    if(soil_crc16_ccitt_false(p->buf, 1 + p->len) != rx_crc) return false;
    if(p->len < 4) return false; // too short to hold moisture + temperature
    uint16_t moisture = (uint16_t)(p->buf[1] | (p->buf[2] << 8));
    if(moisture > 1000) return false;
    reading->moisture = moisture;
    reading->temperature_dc = (int16_t)(p->buf[3] | (p->buf[4] << 8));
    return true;
}

bool soil_parser_feed(SoilParser* p, uint8_t byte, SoilReading* reading) {
    switch(p->state) {
    case SoilStateSync:
        // Bytes skipped while hunting for sync are not counted: counting every
        // garbage byte would make the error count meaningless.
        if(byte == SOIL_SYNC) p->state = SoilStateLen;
        break;
    case SoilStateLen:
        if(byte > SOIL_MAX_PAYLOAD) {
            p->error_count++;
            // The byte may itself be a sync byte.
            p->state = (byte == SOIL_SYNC) ? SoilStateLen : SoilStateSync;
        } else {
            p->len = byte;
            p->buf[0] = byte;
            p->pos = 0;
            p->state = byte ? SoilStatePayload : SoilStateCrc0;
        }
        break;
    case SoilStatePayload:
        p->buf[1 + p->pos++] = byte;
        if(p->pos >= p->len) p->state = SoilStateCrc0;
        break;
    case SoilStateCrc0:
        p->crc_lo = byte;
        p->state = SoilStateCrc1;
        break;
    case SoilStateCrc1: {
        // CRC transmitted little endian.
        uint16_t rx_crc = (uint16_t)(p->crc_lo | (byte << 8));
        p->state = SoilStateSync;
        if(soil_try_frame(p, rx_crc, reading)) {
            p->ok_count++;
            return true;
        }
        p->error_count++;
        break;
    }
    }
    return false;
}
