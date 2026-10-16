#include "ir_parser.h"

#include <stdio.h>
#include <string.h>

enum {
    SeenName = 1 << 0,
    SeenType = 1 << 1,
    SeenProtocol = 1 << 2,
    SeenAddress = 1 << 3,
    SeenCommand = 1 << 4,
    SeenFrequency = 1 << 5,
    SeenDuty = 1 << 6,
    SeenData = 1 << 7,
};

#define SEEN_PARSED_REQUIRED (SeenName | SeenType | SeenProtocol | SeenAddress | SeenCommand)
#define SEEN_RAW_REQUIRED    (SeenName | SeenType | SeenFrequency | SeenDuty | SeenData)

static IrStatus fail(IrParser* p, IrStatus s) {
    if(p->status == IrStatusOk) {
        p->status = s;
        p->error_line = p->line_no;
    }
    return p->status;
}

static int is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decimal uint32, 1..10 digits, no sign/space, no wrap. */
static int parse_u32(const char* s, size_t len, uint32_t* out) {
    if(len == 0 || len > 10) return 0;
    uint64_t v = 0;
    for(size_t i = 0; i < len; i++) {
        if(!is_digit(s[i])) return 0;
        v = v * 10 + (uint64_t)(s[i] - '0');
    }
    if(v > UINT32_MAX) return 0;
    *out = (uint32_t)v;
    return 1;
}

/* Exactly "XX XX XX XX". */
static int parse_hex4(const char* s, size_t len, uint8_t out[4]) {
    if(len != 11) return 0;
    for(size_t i = 0; i < 4; i++) {
        int hi = hex_val(s[i * 3]);
        int lo = hex_val(s[i * 3 + 1]);
        if(hi < 0 || lo < 0) return 0;
        if(i < 3 && s[i * 3 + 2] != ' ') return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

/* "D", "D.DDD..." with D in 0..1; stored in millionths, extra digits truncated. */
static int parse_duty(const char* s, size_t len, uint32_t* out) {
    if(len == 0 || len > 16 || !is_digit(s[0])) return 0;
    uint32_t ppm = (uint32_t)(s[0] - '0') * 1000000u;
    size_t i = 1;
    if(i < len) {
        if(s[i] != '.' || i + 1 >= len) return 0;
        uint32_t scale = 100000u;
        for(i++; i < len; i++) {
            if(!is_digit(s[i])) return 0;
            ppm += (uint32_t)(s[i] - '0') * scale;
            scale /= 10;
        }
    }
    if(ppm == 0 || ppm > 1000000u) return 0;
    *out = ppm;
    return 1;
}

static int key_is(const char* key, size_t klen, const char* lit) {
    return strlen(lit) == klen && memcmp(key, lit, klen) == 0;
}

static IrStatus finalize_signal(IrParser* p) {
    uint8_t need = (p->cur.type == IrTypeParsed) ? SEEN_PARSED_REQUIRED : SEEN_RAW_REQUIRED;
    if((p->seen & SeenType) == 0 || (p->seen & need) != need) {
        return fail(p, IrStatusMissingField);
    }
    if(p->out->count >= IR_MAX_BUTTONS) return fail(p, IrStatusTooManyButtons);
    p->out->buttons[p->out->count++] = p->cur;
    p->cur_open = 0;
    p->seen = 0;
    return p->status;
}

static IrStatus handle_header(IrParser* p, const char* key, size_t klen, const char* val, size_t vlen) {
    if(p->header_stage == 0) {
        if(!key_is(key, klen, "Filetype")) return fail(p, IrStatusBadFiletype);
        if(vlen != strlen("IR signals file") || memcmp(val, "IR signals file", vlen) != 0) {
            return fail(p, IrStatusBadFiletype);
        }
    } else {
        if(!key_is(key, klen, "Version")) return fail(p, IrStatusBadHeader);
        uint32_t v;
        if(!parse_u32(val, vlen, &v) || v != 1) return fail(p, IrStatusBadVersion);
    }
    p->header_stage++;
    return p->status;
}

static IrStatus set_seen(IrParser* p, uint8_t bit) {
    if(p->seen & bit) return fail(p, IrStatusDuplicateKey);
    p->seen |= bit;
    return p->status;
}

static IrStatus handle_body(IrParser* p, const char* key, size_t klen, const char* val, size_t vlen) {
    if(key_is(key, klen, "name")) {
        if(p->cur_open && finalize_signal(p) != IrStatusOk) return p->status;
        if(vlen > IR_NAME_MAX) return fail(p, IrStatusBadValue);
        memset(&p->cur, 0, sizeof(p->cur));
        memcpy(p->cur.name, val, vlen);
        p->cur_open = 1;
        p->seen = SeenName;
        return p->status;
    }

    int known = key_is(key, klen, "type") || key_is(key, klen, "protocol") ||
                key_is(key, klen, "address") || key_is(key, klen, "command") ||
                key_is(key, klen, "frequency") || key_is(key, klen, "duty_cycle") ||
                key_is(key, klen, "data");
    if(!known) return fail(p, IrStatusUnsupportedKey);
    if(!p->cur_open) return fail(p, IrStatusBadOrder);

    if(key_is(key, klen, "type")) {
        if(set_seen(p, SeenType) != IrStatusOk) return p->status;
        if(vlen == 6 && memcmp(val, "parsed", 6) == 0) {
            p->cur.type = IrTypeParsed;
        } else if(vlen == 3 && memcmp(val, "raw", 3) == 0) {
            p->cur.type = IrTypeRaw;
        } else {
            return fail(p, IrStatusBadValue);
        }
        return p->status;
    }

    if(!(p->seen & SeenType)) return fail(p, IrStatusBadOrder);
    int parsed = (p->cur.type == IrTypeParsed);

    if(key_is(key, klen, "protocol")) {
        if(!parsed) return fail(p, IrStatusBadOrder);
        if(set_seen(p, SeenProtocol) != IrStatusOk) return p->status;
        if(vlen == 0 || vlen > IR_PROTOCOL_MAX) return fail(p, IrStatusBadValue);
        for(size_t i = 0; i < vlen; i++) {
            char c = val[i];
            if(!(is_digit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' ||
                 c == '-')) {
                return fail(p, IrStatusBadValue);
            }
        }
        memcpy(p->cur.protocol, val, vlen);
    } else if(key_is(key, klen, "address") || key_is(key, klen, "command")) {
        if(!parsed) return fail(p, IrStatusBadOrder);
        int is_addr = key_is(key, klen, "address");
        if(set_seen(p, is_addr ? SeenAddress : SeenCommand) != IrStatusOk) return p->status;
        if(!parse_hex4(val, vlen, is_addr ? p->cur.address : p->cur.command)) {
            return fail(p, IrStatusBadValue);
        }
    } else if(key_is(key, klen, "frequency")) {
        if(parsed) return fail(p, IrStatusBadOrder);
        if(set_seen(p, SeenFrequency) != IrStatusOk) return p->status;
        uint32_t f;
        if(!parse_u32(val, vlen, &f) || f < IR_MIN_FREQUENCY || f > IR_MAX_FREQUENCY) {
            return fail(p, IrStatusBadValue);
        }
        p->cur.frequency = f;
    } else if(key_is(key, klen, "duty_cycle")) {
        if(parsed) return fail(p, IrStatusBadOrder);
        if(set_seen(p, SeenDuty) != IrStatusOk) return p->status;
        if(!parse_duty(val, vlen, &p->cur.duty_ppm)) return fail(p, IrStatusBadValue);
    } else {
        /* "data": the streaming path handles valid ones before we get here. */
        if(parsed || (p->seen & SeenData)) {
            return fail(p, parsed ? IrStatusBadOrder : IrStatusDuplicateKey);
        }
        return fail(p, IrStatusBadValue);
    }
    return p->status;
}

static IrStatus end_line(IrParser* p) {
    size_t len = p->line_len;
    p->line_len = 0;
    if(len > 0 && p->line[len - 1] == '\r') len--;
    p->line[len] = '\0';

    if(len == 0 || p->line[0] == '#') return p->status; /* blank / comment */

    for(size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p->line[i];
        if(c < 0x20 || c > 0x7e) return fail(p, IrStatusBadChar);
    }

    const char* colon = memchr(p->line, ':', len);
    if(!colon || (size_t)(colon - p->line) + 1 >= len || colon[1] != ' ') {
        return fail(p, IrStatusSyntax);
    }
    size_t klen = (size_t)(colon - p->line);
    const char* val = colon + 1;
    while(*val == ' ') val++;
    size_t vlen = len - (size_t)(val - p->line);
    if(klen == 0 || vlen == 0) return fail(p, IrStatusSyntax);

    if(p->header_stage < 2) return handle_header(p, p->line, klen, val, vlen);
    return handle_body(p, p->line, klen, val, vlen);
}

static IrStatus flush_timing(IrParser* p) {
    if(p->tok_len == 0) return p->status;
    uint32_t v;
    if(!parse_u32(p->tok, p->tok_len, &v) || v == 0) return fail(p, IrStatusBadValue);
    p->tok_len = 0;
    if(p->cur.timing_count >= IR_MAX_TIMINGS) return fail(p, IrStatusTooManyTimings);
    p->cur.timing_count++;
    return p->status;
}

static IrStatus end_data_line(IrParser* p) {
    if(flush_timing(p) != IrStatusOk) return p->status;
    if(p->cur.timing_count == 0) return fail(p, IrStatusBadValue);
    p->in_data = 0;
    p->seen |= SeenData;
    return p->status;
}

static void feed_byte(IrParser* p, char c) {
    if(p->in_data) {
        if(c == '\n') {
            end_data_line(p);
            p->line_no++;
        } else if(!p->data_started) {
            if(c != ' ') fail(p, IrStatusBadValue);
            p->data_started = 1;
        } else if(is_digit(c)) {
            if(p->tok_len >= 10) {
                fail(p, IrStatusBadValue);
            } else {
                p->tok[p->tok_len++] = c;
            }
        } else if(c == ' ' || c == '\r') {
            flush_timing(p);
        } else {
            fail(p, IrStatusBadValue);
        }
        return;
    }

    if(c == '\n') {
        end_line(p);
        p->line_no++;
        return;
    }
    if(p->line_len >= IR_LINE_MAX) {
        fail(p, IrStatusLineTooLong);
        return;
    }
    p->line[p->line_len++] = c;

    /* Enter streaming mode so long timing lists need no buffer. */
    if(p->line_len == 5 && memcmp(p->line, "data:", 5) == 0 && p->header_stage == 2 &&
       p->cur_open && (p->seen & SeenType) && p->cur.type == IrTypeRaw && !(p->seen & SeenData)) {
        p->in_data = 1;
        p->data_started = 0;
        p->tok_len = 0;
        p->cur.timing_count = 0;
        p->line_len = 0;
    }
}

void ir_parser_init(IrParser* p, IrFile* out) {
    memset(p, 0, sizeof(*p));
    p->out = out;
    p->line_no = 1;
    out->count = 0;
}

IrStatus ir_parser_feed(IrParser* p, const uint8_t* data, size_t len) {
    if(p->status != IrStatusOk) return p->status;
    if(p->finished) return p->status;
    if(len > IR_MAX_FILE_BYTES - p->total_bytes) return fail(p, IrStatusFileTooLarge);
    p->total_bytes += len;
    for(size_t i = 0; i < len && p->status == IrStatusOk; i++) {
        feed_byte(p, (char)data[i]);
    }
    return p->status;
}

IrStatus ir_parser_finish(IrParser* p) {
    if(p->finished || p->status != IrStatusOk) {
        p->finished = 1;
        return p->status;
    }
    p->finished = 1;

    if(p->in_data) {
        end_data_line(p);
    } else if(p->line_len > 0) {
        end_line(p);
    }
    if(p->status != IrStatusOk) return p->status;

    if(p->header_stage == 0) return fail(p, IrStatusEmpty);
    if(p->header_stage == 1) return fail(p, IrStatusBadVersion);
    if(p->cur_open) finalize_signal(p);
    return p->status;
}

IrStatus ir_parse_buffer(const uint8_t* data, size_t len, IrFile* out, uint32_t* error_line) {
    IrParser p;
    ir_parser_init(&p, out);
    ir_parser_feed(&p, data, len);
    IrStatus s = ir_parser_finish(&p);
    if(error_line) *error_line = (s == IrStatusOk) ? 0 : p.error_line;
    return s;
}

const char* ir_status_str(IrStatus s) {
    switch(s) {
    case IrStatusOk: return "ok";
    case IrStatusEmpty: return "empty file";
    case IrStatusBadFiletype: return "not an IR signals file";
    case IrStatusBadVersion: return "unsupported or missing version";
    case IrStatusBadHeader: return "malformed header";
    case IrStatusLineTooLong: return "line too long";
    case IrStatusBadChar: return "invalid character";
    case IrStatusSyntax: return "syntax error";
    case IrStatusUnsupportedKey: return "unsupported key";
    case IrStatusDuplicateKey: return "duplicate key";
    case IrStatusBadOrder: return "key out of order";
    case IrStatusBadValue: return "invalid value";
    case IrStatusMissingField: return "missing field";
    case IrStatusTooManyTimings: return "too many timings";
    case IrStatusTooManyButtons: return "too many buttons";
    case IrStatusFileTooLarge: return "file too large";
    }
    return "unknown error";
}

void ir_button_summary(const IrButton* b, char* buf, size_t buf_size) {
    if(!buf || buf_size == 0) return;
    if(b->type == IrTypeParsed) {
        snprintf(buf, buf_size, "Parsed %s", b->protocol);
    } else {
        snprintf(
            buf,
            buf_size,
            "Raw %luHz %lu timings",
            (unsigned long)b->frequency,
            (unsigned long)b->timing_count);
    }
}
