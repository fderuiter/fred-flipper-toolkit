#include "ir_parser.h"

#include <stdio.h>
#include <string.h>

enum { ST_LINE_START, ST_COMMENT, ST_KEY, ST_AFTER_COLON, ST_VALUE };

enum {
    K_NONE,
    K_FILETYPE,
    K_VERSION,
    K_NAME,
    K_TYPE,
    K_PROTOCOL,
    K_ADDRESS,
    K_COMMAND,
    K_FREQUENCY,
    K_DUTY,
    K_DATA,
};

#define BIT(k) ((uint16_t)(1u << (k)))
#define PARSED_KEYS (BIT(K_PROTOCOL) | BIT(K_ADDRESS) | BIT(K_COMMAND))
#define RAW_KEYS (BIT(K_FREQUENCY) | BIT(K_DUTY) | BIT(K_DATA))
#define IR_KEY_MAX 15
#define IR_TOKEN_MAX_DIGITS 10

static const struct {
    const char* name;
    uint8_t id;
} body_keys[] = {
    {"name", K_NAME},
    {"type", K_TYPE},
    {"protocol", K_PROTOCOL},
    {"address", K_ADDRESS},
    {"command", K_COMMAND},
    {"frequency", K_FREQUENCY},
    {"duty_cycle", K_DUTY},
    {"data", K_DATA},
};

static IrStatus fail(IrParser* p, IrStatus st) {
    if(p->status == IR_OK) {
        p->status = st;
        p->err_line = p->line;
    }
    return p->status;
}

static IrSignal* cur(IrParser* p) {
    return &p->out->signals[p->out->count];
}

/* Block is complete and consistent with its type. */
static IrStatus check_block(const IrParser* p) {
    uint16_t s = p->seen;
    if(!(s & BIT(K_TYPE))) return IR_ERR_MISSING_KEY;
    const IrSignal* sig = &p->out->signals[p->out->count];
    uint16_t need = sig->type == IR_TYPE_PARSED ? PARSED_KEYS : RAW_KEYS;
    uint16_t other = sig->type == IR_TYPE_PARSED ? RAW_KEYS : PARSED_KEYS;
    if(s & other) return IR_ERR_UNKNOWN_KEY;
    if((s & need) != need) return IR_ERR_MISSING_KEY;
    return IR_OK;
}

static IrStatus finalize_block(IrParser* p) {
    if(!p->cur_open) return IR_OK;
    IrStatus st = check_block(p);
    if(st != IR_OK) return fail(p, st);
    p->out->count++;
    p->cur_open = 0;
    p->seen = 0;
    return IR_OK;
}

static IrStatus begin_key(IrParser* p) {
    p->key[p->key_len] = '\0';
    p->key_id = K_NONE;
    p->val_len = 0;
    p->tok_digits = 0;
    p->tok_val = 0;

    if(p->stage == 0) {
        if(strcmp(p->key, "Filetype") != 0) return fail(p, IR_ERR_HEADER);
        p->key_id = K_FILETYPE;
        return IR_OK;
    }
    if(p->stage == 1) {
        if(strcmp(p->key, "Version") != 0) return fail(p, IR_ERR_HEADER);
        p->key_id = K_VERSION;
        return IR_OK;
    }

    for(size_t i = 0; i < sizeof(body_keys) / sizeof(body_keys[0]); i++) {
        if(strcmp(p->key, body_keys[i].name) == 0) p->key_id = body_keys[i].id;
    }
    if(p->key_id == K_NONE) return fail(p, IR_ERR_UNKNOWN_KEY);

    if(p->key_id == K_NAME) {
        if(p->cur_open) {
            /* A new name without '#': fine only if the previous block is done. */
            if(check_block(p) != IR_OK) return fail(p, IR_ERR_DUPLICATE_KEY);
            if(finalize_block(p) != IR_OK) return p->status;
        }
        if(p->out->count >= IR_MAX_SIGNALS) return fail(p, IR_ERR_TOO_MANY_SIGNALS);
        memset(cur(p), 0, sizeof(IrSignal));
        p->cur_open = 1;
        p->seen = 0;
    } else if(!p->cur_open) {
        return fail(p, IR_ERR_SYNTAX);
    }
    if(p->seen & BIT(p->key_id)) return fail(p, IR_ERR_DUPLICATE_KEY);
    p->seen |= BIT(p->key_id);
    return IR_OK;
}

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "XX XX XX XX" exactly. */
static int parse_hex4(const char* s, size_t len, uint8_t out[4]) {
    if(len != 11) return 0;
    for(size_t i = 0; i < 4; i++) {
        int hi = hex_val(s[i * 3]);
        int lo = hex_val(s[i * 3 + 1]);
        if(hi < 0 || lo < 0) return 0;
        if(i < 3 && s[i * 3 + 2] != ' ') return 0;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return 1;
}

/* Decimal u32. Returns 0 ok, 1 malformed, 2 overflow. */
static int parse_u32(const char* s, size_t len, uint32_t* out) {
    if(len == 0) return 1;
    uint64_t v = 0;
    for(size_t i = 0; i < len; i++) {
        if(s[i] < '0' || s[i] > '9') return 1;
        v = v * 10 + (uint64_t)(s[i] - '0');
        if(v > 0xFFFFFFFFull) return 2;
    }
    *out = (uint32_t)v;
    return 0;
}

/* "0.33", "1", "0.330000" -> parts per million, 1..1000000. */
static IrStatus parse_duty(const char* s, size_t len, uint32_t* out) {
    size_t i = 0;
    uint32_t whole = 0, frac = 0, scale = 100000;
    size_t whole_digits = 0;
    for(; i < len && s[i] >= '0' && s[i] <= '9'; i++) {
        if(++whole_digits > 1) return IR_ERR_RANGE;
        whole = (uint32_t)(s[i] - '0');
    }
    if(whole_digits == 0) return IR_ERR_BAD_VALUE;
    if(i < len) {
        if(s[i] != '.') return IR_ERR_BAD_VALUE;
        i++;
        if(i == len) return IR_ERR_BAD_VALUE;
        for(; i < len; i++) {
            if(s[i] < '0' || s[i] > '9') return IR_ERR_BAD_VALUE;
            if(scale > 0) {
                frac += (uint32_t)(s[i] - '0') * scale;
                scale /= 10;
            } else if(s[i] != '0') {
                return IR_ERR_RANGE; /* more than 6 significant decimals */
            }
        }
    }
    uint32_t ppm = whole * 1000000u + frac;
    if(ppm == 0 || ppm > 1000000u) return IR_ERR_RANGE;
    *out = ppm;
    return IR_OK;
}

static IrStatus finish_token(IrParser* p) {
    if(p->tok_digits == 0) return IR_OK;
    if(p->tok_val == 0) return fail(p, IR_ERR_BAD_VALUE);
    IrSignal* s = cur(p);
    if(s->timing_count >= IR_MAX_TIMINGS) return fail(p, IR_ERR_TOO_MANY_TIMINGS);
    s->timing_count++;
    p->tok_digits = 0;
    p->tok_val = 0;
    return IR_OK;
}

static IrStatus value_char(IrParser* p, char c) {
    if(p->key_id == K_DATA) {
        if(c == ' ') return finish_token(p);
        if(c < '0' || c > '9') return fail(p, IR_ERR_BAD_VALUE);
        if(++p->tok_digits > IR_TOKEN_MAX_DIGITS) return fail(p, IR_ERR_RANGE);
        uint64_t v = (uint64_t)p->tok_val * 10 + (uint64_t)(c - '0');
        if(v > 0xFFFFFFFFull) return fail(p, IR_ERR_RANGE);
        p->tok_val = (uint32_t)v;
        return IR_OK;
    }
    if(p->val_len >= IR_NAME_MAX) return fail(p, IR_ERR_TOO_LONG);
    p->val[p->val_len++] = c;
    return IR_OK;
}

static IrStatus apply_value(IrParser* p) {
    size_t n = p->val_len;
    while(n > 0 && p->val[n - 1] == ' ') n--;
    p->val[n] = '\0';
    if(n == 0) return fail(p, IR_ERR_BAD_VALUE);

    IrSignal* s = cur(p);
    uint32_t v;
    int r;
    IrStatus st;

    switch(p->key_id) {
    case K_FILETYPE:
        if(strcmp(p->val, "IR signals file") != 0) return fail(p, IR_ERR_HEADER);
        p->stage = 1;
        return IR_OK;
    case K_VERSION:
        if(strcmp(p->val, "1") != 0) return fail(p, IR_ERR_VERSION);
        p->stage = 2;
        return IR_OK;
    case K_NAME:
        memcpy(s->name, p->val, n + 1);
        return IR_OK;
    case K_TYPE:
        if(strcmp(p->val, "parsed") == 0) s->type = IR_TYPE_PARSED;
        else if(strcmp(p->val, "raw") == 0) s->type = IR_TYPE_RAW;
        else return fail(p, IR_ERR_BAD_VALUE);
        return IR_OK;
    case K_PROTOCOL:
        if(n > IR_PROTOCOL_MAX) return fail(p, IR_ERR_TOO_LONG);
        for(size_t i = 0; i < n; i++) {
            char c = p->val[i];
            int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-';
            if(!ok) return fail(p, IR_ERR_BAD_VALUE);
        }
        memcpy(s->protocol, p->val, n + 1);
        return IR_OK;
    case K_ADDRESS:
        if(!parse_hex4(p->val, n, s->address)) return fail(p, IR_ERR_BAD_VALUE);
        return IR_OK;
    case K_COMMAND:
        if(!parse_hex4(p->val, n, s->command)) return fail(p, IR_ERR_BAD_VALUE);
        return IR_OK;
    case K_FREQUENCY:
        r = parse_u32(p->val, n, &v);
        if(r == 1) return fail(p, IR_ERR_BAD_VALUE);
        if(r == 2 || v < IR_FREQ_MIN || v > IR_FREQ_MAX) return fail(p, IR_ERR_RANGE);
        s->frequency = v;
        return IR_OK;
    case K_DUTY:
        st = parse_duty(p->val, n, &v);
        if(st != IR_OK) return fail(p, st);
        s->duty_cycle_ppm = v;
        return IR_OK;
    case K_DATA:
        if(finish_token(p) != IR_OK) return p->status;
        if(s->timing_count == 0) return fail(p, IR_ERR_BAD_VALUE);
        return IR_OK;
    default:
        return fail(p, IR_ERR_SYNTAX);
    }
}

static IrStatus end_line(IrParser* p) {
    switch(p->state) {
    case ST_LINE_START:
        if(p->stage < 2) return fail(p, IR_ERR_HEADER);
        return IR_OK; /* blank line */
    case ST_COMMENT:
        return IR_OK;
    case ST_KEY:
        return fail(p, IR_ERR_SYNTAX);
    case ST_AFTER_COLON:
        return fail(p, IR_ERR_BAD_VALUE);
    default:
        if(p->key_id == K_DATA) {
            /* data has no val buffer; apply_value only finishes the last token */
            p->val_len = 1;
            p->val[0] = 'x';
        }
        return apply_value(p);
    }
}

static IrStatus step(IrParser* p, char c) {
    unsigned char u = (unsigned char)c;

    if(p->state == ST_COMMENT) {
        if(c == '\n') {
            p->line++;
            p->state = ST_LINE_START;
        }
        return IR_OK;
    }
    if(p->cr_pending && c != '\n') return fail(p, IR_ERR_BAD_CHAR);
    if(c == '\r') {
        p->cr_pending = 1;
        return IR_OK;
    }
    if(c == '\n') {
        IrStatus st = end_line(p);
        if(st != IR_OK) return st;
        p->line++;
        p->cr_pending = 0;
        p->state = ST_LINE_START;
        return IR_OK;
    }
    if(u < 0x20 || u > 0x7E) return fail(p, IR_ERR_BAD_CHAR);

    switch(p->state) {
    case ST_LINE_START:
        if(c == '#') {
            if(p->stage < 2) return fail(p, IR_ERR_HEADER);
            if(finalize_block(p) != IR_OK) return p->status;
            p->state = ST_COMMENT;
            return IR_OK;
        }
        p->key_len = 0;
        p->state = ST_KEY;
        /* fall through */
    case ST_KEY:
        if(c == ':' && p->key_len > 0) {
            if(begin_key(p) != IR_OK) return p->status;
            p->state = ST_AFTER_COLON;
            return IR_OK;
        }
        if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'))
            return fail(p, IR_ERR_SYNTAX);
        if(p->key_len >= IR_KEY_MAX) return fail(p, IR_ERR_UNKNOWN_KEY);
        p->key[p->key_len++] = c;
        return IR_OK;
    case ST_AFTER_COLON:
        if(c == ' ') return IR_OK;
        p->state = ST_VALUE;
        /* fall through */
    default:
        return value_char(p, c);
    }
}

void ir_parser_init(IrParser* p, IrFile* out) {
    memset(p, 0, sizeof(*p));
    p->out = out;
    p->line = 1;
    out->count = 0;
}

IrStatus ir_parser_feed(IrParser* p, const char* data, size_t len) {
    if(len > 0) p->any_input = 1;
    for(size_t i = 0; i < len && p->status == IR_OK; i++) step(p, data[i]);
    return p->status;
}

IrStatus ir_parser_finish(IrParser* p) {
    if(p->status != IR_OK) return p->status;
    if(!p->any_input) return fail(p, IR_ERR_EMPTY);

    if(p->state != ST_LINE_START && p->state != ST_COMMENT) {
        /* Input ended mid-line: whatever is wrong with it is truncation. */
        IrStatus st = end_line(p);
        if(st != IR_OK) {
            p->status = IR_ERR_TRUNCATED;
            return p->status;
        }
    }
    if(p->stage < 2) return fail(p, IR_ERR_TRUNCATED);
    if(finalize_block(p) != IR_OK) return p->status;
    return IR_OK;
}

IrStatus ir_parse_buffer(const char* data, size_t len, IrFile* out, uint32_t* err_line) {
    IrParser p;
    ir_parser_init(&p, out);
    ir_parser_feed(&p, data, len);
    IrStatus st = ir_parser_finish(&p);
    if(err_line) *err_line = p.err_line;
    return st;
}

uint32_t ir_parser_error_line(const IrParser* p) {
    return p->err_line;
}

const char* ir_status_str(IrStatus st) {
    switch(st) {
    case IR_OK: return "ok";
    case IR_ERR_EMPTY: return "empty file";
    case IR_ERR_HEADER: return "bad header";
    case IR_ERR_VERSION: return "unsupported version";
    case IR_ERR_SYNTAX: return "syntax error";
    case IR_ERR_UNKNOWN_KEY: return "unsupported key";
    case IR_ERR_DUPLICATE_KEY: return "duplicate key";
    case IR_ERR_MISSING_KEY: return "missing key";
    case IR_ERR_BAD_VALUE: return "bad value";
    case IR_ERR_RANGE: return "value out of range";
    case IR_ERR_TOO_LONG: return "value too long";
    case IR_ERR_BAD_CHAR: return "invalid character";
    case IR_ERR_TOO_MANY_TIMINGS: return "too many timings";
    case IR_ERR_TOO_MANY_SIGNALS: return "too many signals";
    case IR_ERR_TRUNCATED: return "truncated file";
    }
    return "unknown error";
}

char* ir_signal_describe(const IrSignal* s, char* buf, size_t buf_size) {
    if(buf_size == 0) return buf;
    if(s->type == IR_TYPE_PARSED) {
        snprintf(buf, buf_size, "parsed %s", s->protocol);
    } else {
        snprintf(
            buf,
            buf_size,
            "raw %luHz %lu timings",
            (unsigned long)s->frequency,
            (unsigned long)s->timing_count);
    }
    buf[buf_size - 1] = '\0';
    return buf;
}
