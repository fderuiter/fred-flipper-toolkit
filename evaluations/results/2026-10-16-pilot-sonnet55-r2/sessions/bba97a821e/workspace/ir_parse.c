#include "ir_parse.h"

#include <string.h>

enum { PH_FILETYPE = 0, PH_VERSION, PH_BODY };
enum { LS_START = 0, LS_COMMENT, LS_KEY, LS_VALUE, LS_DATA };
enum {
    K_FILETYPE = 1, K_VERSION, K_NAME, K_TYPE, K_PROTOCOL,
    K_ADDRESS, K_COMMAND, K_FREQUENCY, K_DUTY, K_DATA
};
#define BIT(k) ((uint16_t)(1u << (k)))

#define M_PARSED (BIT(K_PROTOCOL) | BIT(K_ADDRESS) | BIT(K_COMMAND))
#define M_RAW (BIT(K_FREQUENCY) | BIT(K_DUTY) | BIT(K_DATA))

static const struct {
    const char* name;
    uint8_t id;
} KEYS[] = {
    {"Filetype", K_FILETYPE}, {"Version", K_VERSION}, {"name", K_NAME},
    {"type", K_TYPE},         {"protocol", K_PROTOCOL}, {"address", K_ADDRESS},
    {"command", K_COMMAND},   {"frequency", K_FREQUENCY}, {"duty_cycle", K_DUTY},
    {"data", K_DATA},
};

static ir_status_t fail(ir_parser_t* p, ir_status_t s) {
    if(p->status == IR_OK) {
        p->status = s;
        p->error_line = p->line;
    }
    return p->status;
}

static int lookup_key(const char* k) {
    for(size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if(strcmp(KEYS[i].name, k) == 0) return KEYS[i].id;
    }
    return 0;
}

/* Decimal digits only, no sign, no overflow past uint32. */
static int parse_u32(const char* s, uint32_t* out) {
    if(*s == '\0') return 0;
    uint64_t v = 0;
    for(; *s; s++) {
        if(*s < '0' || *s > '9') return 0;
        v = v * 10u + (uint64_t)(*s - '0');
        if(v > UINT32_MAX) return 0;
    }
    *out = (uint32_t)v;
    return 1;
}

/* "0.33", "1", "0.330000" -> millionths. Locale-independent, no floats. */
static int parse_duty(const char* s, uint32_t* out) {
    uint32_t whole = 0, frac = 0, scale = 100000u;
    int digits = 0;
    for(; *s >= '0' && *s <= '9'; s++, digits++) {
        if(digits >= 3) return 0;
        whole = whole * 10u + (uint32_t)(*s - '0');
    }
    if(digits == 0) return 0;
    if(*s == '.') {
        s++;
        int fd = 0;
        for(; *s >= '0' && *s <= '9'; s++, fd++) {
            if(scale) { /* digits past 6 places are ignored */
                frac += (uint32_t)(*s - '0') * scale;
                scale /= 10u;
            }
        }
        if(fd == 0) return 0;
    }
    if(*s != '\0') return 0;
    if(whole > 1) return 0;
    uint32_t v = whole * 1000000u + frac;
    if(v == 0 || v > 1000000u) return 0;
    *out = v;
    return 1;
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly four "XX" bytes separated by single spaces. */
static int parse_hex4(const char* s, uint8_t out[4]) {
    if(strlen(s) != 11) return 0;
    for(int i = 0; i < 4; i++) {
        int hi = hex_nibble(s[i * 3]), lo = hex_nibble(s[i * 3 + 1]);
        if(hi < 0 || lo < 0) return 0;
        if(i < 3 && s[i * 3 + 2] != ' ') return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static int is_token_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static ir_status_t finalize_signal(ir_parser_t* p) {
    if(!p->in_signal) return p->status;
    ir_signal_t* s = &p->out[p->count];
    uint16_t seen = p->seen;
    uint16_t need, forbid;
    if(!(seen & BIT(K_TYPE))) return fail(p, IR_ERR_INCOMPLETE);
    if(s->type == IR_SIGNAL_PARSED) {
        need = M_PARSED;
        forbid = M_RAW;
    } else {
        need = M_RAW;
        forbid = M_PARSED;
    }
    if((seen & need) != need || (seen & forbid)) return fail(p, IR_ERR_INCOMPLETE);
    if(s->type == IR_SIGNAL_RAW && s->timing_count == 0) return fail(p, IR_ERR_INCOMPLETE);
    p->in_signal = 0;
    p->count++;
    return p->status;
}

static ir_status_t handle_value(ir_parser_t* p) {
    /* trim trailing blanks; vbuf is NUL-terminated */
    while(p->vlen > 0 && (p->vbuf[p->vlen - 1] == ' ' || p->vbuf[p->vlen - 1] == '\t')) p->vlen--;
    p->vbuf[p->vlen] = '\0';
    const char* v = p->vbuf;
    uint8_t k = p->key;

    if(p->phase == PH_FILETYPE) {
        if(k != K_FILETYPE || strcmp(v, "IR signals file") != 0)
            return fail(p, IR_ERR_BAD_FILETYPE);
        p->phase = PH_VERSION;
        return p->status;
    }
    if(p->phase == PH_VERSION) {
        if(k != K_VERSION || strcmp(v, "1") != 0) return fail(p, IR_ERR_UNSUPPORTED_VERSION);
        p->phase = PH_BODY;
        return p->status;
    }

    if(k == K_FILETYPE || k == K_VERSION) return fail(p, IR_ERR_DUPLICATE_KEY);

    if(k == K_NAME) {
        if(finalize_signal(p) != IR_OK) return p->status;
        if(p->count >= p->capacity) return fail(p, IR_ERR_TOO_MANY_SIGNALS);
        size_t n = p->vlen;
        if(n == 0 || n > IR_NAME_MAX) return fail(p, IR_ERR_BAD_VALUE);
        for(size_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)v[i];
            if(c < 0x20 || c > 0x7e) return fail(p, IR_ERR_BAD_VALUE);
        }
        ir_signal_t* s = &p->out[p->count];
        memset(s, 0, sizeof(*s));
        memcpy(s->name, v, n);
        p->in_signal = 1;
        p->seen = BIT(K_NAME);
        return p->status;
    }

    if(!p->in_signal) return fail(p, IR_ERR_SYNTAX); /* field before first name */
    if(p->seen & BIT(k)) return fail(p, IR_ERR_DUPLICATE_KEY);
    p->seen |= BIT(k);
    ir_signal_t* s = &p->out[p->count];

    switch(k) {
    case K_TYPE:
        if(strcmp(v, "parsed") == 0) s->type = IR_SIGNAL_PARSED;
        else if(strcmp(v, "raw") == 0) s->type = IR_SIGNAL_RAW;
        else return fail(p, IR_ERR_BAD_VALUE);
        break;
    case K_PROTOCOL:
        if(p->vlen == 0 || p->vlen > IR_PROTOCOL_MAX) return fail(p, IR_ERR_BAD_VALUE);
        for(size_t i = 0; i < p->vlen; i++)
            if(!is_token_char(v[i])) return fail(p, IR_ERR_BAD_VALUE);
        memcpy(s->protocol, v, p->vlen);
        break;
    case K_ADDRESS:
        if(!parse_hex4(v, s->address)) return fail(p, IR_ERR_BAD_VALUE);
        break;
    case K_COMMAND:
        if(!parse_hex4(v, s->command)) return fail(p, IR_ERR_BAD_VALUE);
        break;
    case K_FREQUENCY:
        if(!parse_u32(v, &s->frequency) || s->frequency < IR_FREQ_MIN || s->frequency > IR_FREQ_MAX)
            return fail(p, IR_ERR_BAD_VALUE);
        break;
    case K_DUTY:
        if(!parse_duty(v, &s->duty_ppm)) return fail(p, IR_ERR_BAD_VALUE);
        break;
    default:
        return fail(p, IR_ERR_UNKNOWN_KEY);
    }
    return p->status;
}

static ir_status_t data_token_end(ir_parser_t* p) {
    if(p->tok_digits == 0) return p->status;
    p->tok_digits = 0;
    if(p->tok == 0) return fail(p, IR_ERR_BAD_VALUE);
    ir_signal_t* s = &p->out[p->count];
    if(s->timing_count >= IR_MAX_TIMINGS) return fail(p, IR_ERR_TOO_MANY_TIMINGS);
    s->timing_count++;
    return p->status;
}

static void end_of_line(ir_parser_t* p) {
    switch(p->lstate) {
    case LS_KEY:
        fail(p, IR_ERR_SYNTAX); /* no ':' */
        break;
    case LS_VALUE:
        handle_value(p);
        break;
    case LS_DATA:
        data_token_end(p);
        break;
    default:
        break;
    }
    p->lstate = LS_START;
    p->line++;
}

static void step(ir_parser_t* p, char c) {
    switch(p->lstate) {
    case LS_START:
        if(c == '\n' || c == '\r') {
            if(c == '\n') p->line++;
        } else if(c == '#') {
            p->lstate = LS_COMMENT;
        } else {
            p->lstate = LS_KEY;
            p->klen = 0;
            p->kbuf[p->klen++] = c;
        }
        break;
    case LS_COMMENT:
        break; /* '\n' handled by caller */
    case LS_KEY:
        if(c == ':') {
            p->kbuf[p->klen] = '\0';
            int k = lookup_key(p->kbuf);
            if(k == 0) {
                fail(p, p->phase == PH_BODY ? IR_ERR_UNKNOWN_KEY : IR_ERR_BAD_FILETYPE);
                return;
            }
            p->key = (uint8_t)k;
            p->vlen = 0;
            if(k == K_DATA) {
                if(p->phase != PH_BODY || !p->in_signal) {
                    fail(p, IR_ERR_SYNTAX);
                    return;
                }
                if(p->seen & BIT(K_DATA)) {
                    fail(p, IR_ERR_DUPLICATE_KEY);
                    return;
                }
                p->seen |= BIT(K_DATA);
                p->tok = 0;
                p->tok_digits = 0;
                p->lstate = LS_DATA;
            } else {
                p->lstate = LS_VALUE;
            }
        } else if(p->klen >= sizeof(p->kbuf) - 1) {
            fail(p, p->phase == PH_BODY ? IR_ERR_UNKNOWN_KEY : IR_ERR_BAD_FILETYPE);
        } else {
            p->kbuf[p->klen++] = c;
        }
        break;
    case LS_VALUE:
        if(p->vlen == 0 && (c == ' ' || c == '\t')) break; /* skip leading blanks */
        if(c == '\r') break;
        if(p->vlen >= IR_LINE_VALUE_MAX) {
            fail(p, IR_ERR_LINE_TOO_LONG);
            return;
        }
        p->vbuf[p->vlen++] = c;
        break;
    case LS_DATA:
        if(c >= '0' && c <= '9') {
            uint64_t v = (uint64_t)p->tok * 10u + (uint64_t)(c - '0');
            if(p->tok_digits >= 10 || v > UINT32_MAX) {
                fail(p, IR_ERR_BAD_VALUE);
                return;
            }
            p->tok = (uint32_t)v;
            p->tok_digits++;
        } else if(c == ' ' || c == '\t' || c == '\r') {
            data_token_end(p);
            p->tok = 0;
        } else {
            fail(p, IR_ERR_BAD_VALUE); /* '-', letters, control bytes, ... */
        }
        break;
    }
}

void ir_parser_init(ir_parser_t* p, ir_signal_t* out, size_t capacity) {
    memset(p, 0, sizeof(*p));
    p->out = out;
    p->capacity = capacity;
    p->line = 1;
}

ir_status_t ir_parser_feed(ir_parser_t* p, const char* data, size_t len) {
    for(size_t i = 0; i < len && p->status == IR_OK; i++) {
        char c = data[i];
        if(c == '\n') {
            end_of_line(p);
        } else {
            step(p, c);
        }
    }
    return p->status;
}

ir_status_t ir_parser_finish(ir_parser_t* p) {
    if(p->status != IR_OK) return p->status;
    if(p->lstate != LS_START) end_of_line(p);
    if(p->status != IR_OK) return p->status;
    if(p->phase == PH_FILETYPE) return fail(p, IR_ERR_EMPTY);
    if(p->phase == PH_VERSION) return fail(p, IR_ERR_UNSUPPORTED_VERSION);
    return finalize_signal(p);
}

size_t ir_parser_count(const ir_parser_t* p) {
    return p->count;
}

const char* ir_status_str(ir_status_t s) {
    switch(s) {
    case IR_OK: return "ok";
    case IR_ERR_EMPTY: return "empty file";
    case IR_ERR_BAD_FILETYPE: return "not an IR signals file";
    case IR_ERR_UNSUPPORTED_VERSION: return "unsupported version";
    case IR_ERR_SYNTAX: return "syntax error";
    case IR_ERR_UNKNOWN_KEY: return "unknown key";
    case IR_ERR_DUPLICATE_KEY: return "duplicate key";
    case IR_ERR_BAD_VALUE: return "bad value";
    case IR_ERR_LINE_TOO_LONG: return "line too long";
    case IR_ERR_TOO_MANY_TIMINGS: return "too many timings";
    case IR_ERR_TOO_MANY_SIGNALS: return "too many signals";
    case IR_ERR_INCOMPLETE: return "incomplete signal";
    }
    return "unknown error";
}

ir_status_t ir_parse_buffer(
    const char* data,
    size_t len,
    ir_signal_t* out,
    size_t capacity,
    size_t* count,
    uint32_t* error_line) {
    ir_parser_t p;
    ir_parser_init(&p, out, capacity);
    ir_parser_feed(&p, data, len);
    ir_status_t st = ir_parser_finish(&p);
    if(count) *count = p.count;
    if(error_line) *error_line = p.error_line;
    return st;
}
