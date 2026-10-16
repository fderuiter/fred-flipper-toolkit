#include "ir_parser.h"

#include <stdio.h>
#include <string.h>

enum { /* parser states */
    S_LINE_START = 0,
    S_COMMENT,
    S_KEY,
    S_VALUE_WS,
    S_VALUE,
    S_DATA,
    S_DEAD
};

enum { /* fields seen in the current button */
    F_NAME = 1u << 0,
    F_TYPE = 1u << 1,
    F_PROTOCOL = 1u << 2,
    F_ADDRESS = 1u << 3,
    F_COMMAND = 1u << 4,
    F_FREQUENCY = 1u << 5,
    F_DUTY = 1u << 6,
    F_DATA = 1u << 7
};

#define NEED_PARSED (F_NAME | F_TYPE | F_PROTOCOL | F_ADDRESS | F_COMMAND)
#define NEED_RAW    (F_NAME | F_TYPE | F_FREQUENCY | F_DUTY | F_DATA)

static void fail_at(irv_parser_t* p, irv_error_t err, uint32_t line) {
    if(p->res->error == IRV_OK) {
        p->res->error = err;
        p->res->error_line = line;
    }
    p->state = S_DEAD;
}

/* Record the error and leave the current (void) function. */
#define FAIL(p, err)                  \
    do {                              \
        fail_at((p), (err), (p)->line); \
        return;                       \
    } while(0)

void irv_init(irv_parser_t* p, irv_result_t* res, irv_button_t* storage, size_t capacity) {
    memset(p, 0, sizeof(*p));
    p->res = res;
    p->line = 1;
    res->buttons = storage;
    res->capacity = capacity;
    res->count = 0;
    res->error = IRV_OK;
    res->error_line = 0;
}

/* ---- value parsers ---------------------------------------------------------------- */

static irv_error_t parse_u32(const char* s, size_t n, uint32_t* out) {
    if(n == 0) return IRV_ERR_BAD_NUMBER;
    uint64_t v = 0;
    for(size_t i = 0; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') return IRV_ERR_BAD_NUMBER;
        v = v * 10u + (uint64_t)(s[i] - '0');
        if(v > UINT32_MAX) return IRV_ERR_NUMBER_RANGE;
    }
    *out = (uint32_t)v;
    return IRV_OK;
}

static int hex_digit(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly "XX XX XX XX". */
static irv_error_t parse_hex4(const char* s, size_t n, uint8_t out[4]) {
    if(n != 11) return IRV_ERR_BAD_HEX;
    for(size_t i = 0; i < 4; i++) {
        int hi = hex_digit(s[i * 3]);
        int lo = hex_digit(s[i * 3 + 1]);
        if(hi < 0 || lo < 0) return IRV_ERR_BAD_HEX;
        if(i < 3 && s[i * 3 + 2] != ' ') return IRV_ERR_BAD_HEX;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return IRV_OK;
}

/* "0", "1", "0.33", "0.330000", "1.0" -> millionths, in (0, 1000000]. */
static irv_error_t parse_duty(const char* s, size_t n, uint32_t* out) {
    if(n == 0) return IRV_ERR_BAD_NUMBER;
    if(s[0] != '0' && s[0] != '1') return IRV_ERR_NUMBER_RANGE;
    uint32_t ppm = (uint32_t)(s[0] - '0') * 1000000u;
    bool extra_nonzero = false;
    if(n > 1) {
        if(s[1] != '.' || n == 2) return IRV_ERR_BAD_NUMBER;
        uint32_t scale = 100000u;
        for(size_t i = 2; i < n; i++) {
            if(s[i] < '0' || s[i] > '9') return IRV_ERR_BAD_NUMBER;
            uint32_t d = (uint32_t)(s[i] - '0');
            if(scale > 0) {
                ppm += d * scale;
                scale /= 10u;
            } else if(d != 0) {
                extra_nonzero = true;
            }
        }
    }
    if(ppm == 0 || ppm > 1000000u || (ppm == 1000000u && extra_nonzero))
        return IRV_ERR_NUMBER_RANGE;
    *out = ppm;
    return IRV_OK;
}

static bool valid_protocol(const char* s, size_t n) {
    if(n == 0 || n > IRV_PROTOCOL_MAX) return false;
    for(size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  c == '_' || c == '-';
        if(!ok) return false;
    }
    return true;
}

/* ---- button assembly -------------------------------------------------------------- */

static void commit_button(irv_parser_t* p) {
    if(!p->in_button) return;
    p->in_button = false;
    uint16_t need = 0;
    if(p->seen & F_TYPE) need = (p->cur.type == IRV_SIGNAL_PARSED) ? NEED_PARSED : NEED_RAW;
    if(need == 0 || (p->seen & need) != need) {
        fail_at(p, IRV_ERR_INCOMPLETE, p->button_line);
        return;
    }
    if(p->res->count >= p->res->capacity) {
        fail_at(p, IRV_ERR_TOO_MANY_BUTTONS, p->button_line);
        return;
    }
    p->res->buttons[p->res->count++] = p->cur;
}

/* Returns false (after recording the error) if the field was already present. */
static bool mark(irv_parser_t* p, uint16_t field) {
    if(p->seen & field) {
        fail_at(p, IRV_ERR_DUPLICATE_KEY, p->line);
        return false;
    }
    p->seen |= field;
    return true;
}

/* Field that is only legal for one signal type, and only after "type". */
static bool check_typed(irv_parser_t* p, irv_signal_type_t want) {
    if(!p->in_button || !(p->seen & F_TYPE)) {
        fail_at(p, IRV_ERR_ORDER, p->line);
        return false;
    }
    if(p->cur.type != want) {
        fail_at(p, IRV_ERR_WRONG_FIELD_FOR_TYPE, p->line);
        return false;
    }
    return true;
}

static void handle_header(irv_parser_t* p, const char* val, size_t n) {
    if(p->header == 0) {
        if(strcmp(p->key, "Filetype") != 0 || n != 15 || memcmp(val, "IR signals file", 15) != 0)
            FAIL(p, IRV_ERR_BAD_HEADER);
        p->header = 1;
    } else {
        uint32_t v;
        if(strcmp(p->key, "Version") != 0 || parse_u32(val, n, &v) != IRV_OK)
            FAIL(p, IRV_ERR_BAD_HEADER);
        if(v != 1) FAIL(p, IRV_ERR_UNSUPPORTED_VERSION);
        p->header = 2;
    }
}

static void handle_name(irv_parser_t* p, const char* val, size_t n) {
    if(n == 0) FAIL(p, IRV_ERR_BAD_NAME);
    if(n > IRV_NAME_MAX) FAIL(p, IRV_ERR_NAME_TOO_LONG);
    for(size_t i = 0; i < n; i++) {
        if((uint8_t)val[i] < 0x20 || (uint8_t)val[i] > 0x7e) FAIL(p, IRV_ERR_BAD_NAME);
    }
    commit_button(p);
    if(p->res->error != IRV_OK) return;
    memset(&p->cur, 0, sizeof(p->cur));
    memcpy(p->cur.name, val, n);
    p->in_button = true;
    p->seen = F_NAME;
    p->button_line = p->line;
}

static void end_value(irv_parser_t* p) {
    size_t n = p->vallen;
    while(n > 0 && (p->val[n - 1] == ' ' || p->val[n - 1] == '\t' || p->val[n - 1] == '\r')) n--;
    p->val[n] = '\0';
    const char* val = p->val;
    irv_error_t e;

    if(p->header < 2) {
        handle_header(p, val, n);
        return;
    }

    if(strcmp(p->key, "name") == 0) {
        handle_name(p, val, n);
    } else if(strcmp(p->key, "type") == 0) {
        if(!p->in_button) FAIL(p, IRV_ERR_ORDER);
        if(!mark(p, F_TYPE)) return;
        if(n == 6 && memcmp(val, "parsed", 6) == 0) p->cur.type = IRV_SIGNAL_PARSED;
        else if(n == 3 && memcmp(val, "raw", 3) == 0) p->cur.type = IRV_SIGNAL_RAW;
        else FAIL(p, IRV_ERR_BAD_VALUE);
    } else if(strcmp(p->key, "protocol") == 0) {
        if(!check_typed(p, IRV_SIGNAL_PARSED) || !mark(p, F_PROTOCOL)) return;
        if(!valid_protocol(val, n)) FAIL(p, IRV_ERR_BAD_VALUE);
        memcpy(p->cur.protocol, val, n);
    } else if(strcmp(p->key, "address") == 0) {
        if(!check_typed(p, IRV_SIGNAL_PARSED) || !mark(p, F_ADDRESS)) return;
        e = parse_hex4(val, n, p->cur.address);
        if(e != IRV_OK) FAIL(p, e);
    } else if(strcmp(p->key, "command") == 0) {
        if(!check_typed(p, IRV_SIGNAL_PARSED) || !mark(p, F_COMMAND)) return;
        e = parse_hex4(val, n, p->cur.command);
        if(e != IRV_OK) FAIL(p, e);
    } else if(strcmp(p->key, "frequency") == 0) {
        if(!check_typed(p, IRV_SIGNAL_RAW) || !mark(p, F_FREQUENCY)) return;
        e = parse_u32(val, n, &p->cur.frequency);
        if(e == IRV_OK && (p->cur.frequency < IRV_FREQ_MIN || p->cur.frequency > IRV_FREQ_MAX))
            e = IRV_ERR_NUMBER_RANGE;
        if(e != IRV_OK) FAIL(p, e);
    } else if(strcmp(p->key, "duty_cycle") == 0) {
        if(!check_typed(p, IRV_SIGNAL_RAW) || !mark(p, F_DUTY)) return;
        e = parse_duty(val, n, &p->cur.duty_ppm);
        if(e != IRV_OK) FAIL(p, e);
    } else {
        /* includes "data", which is handled in the S_DATA state and never gets here */
        FAIL(p, IRV_ERR_UNKNOWN_KEY);
    }
}

/* ---- raw "data:" line: streamed, only counted ------------------------------------- */

static void end_token(irv_parser_t* p) {
    if(!p->token_active) return;
    p->token_active = false;
    if(p->token == 0) FAIL(p, IRV_ERR_NUMBER_RANGE);
    if(p->cur.timing_count >= IRV_MAX_TIMINGS) FAIL(p, IRV_ERR_TOO_MANY_TIMINGS);
    p->cur.timing_count++;
}

static void end_data(irv_parser_t* p) {
    if(p->state == S_DEAD) return;
    if(p->cur.timing_count == 0) FAIL(p, IRV_ERR_BAD_VALUE);
    p->seen |= F_DATA;
}

static void end_key(irv_parser_t* p) {
    p->key[p->keylen] = '\0';
    p->vallen = 0;
    if(p->header == 2 && strcmp(p->key, "data") == 0) {
        if(!check_typed(p, IRV_SIGNAL_RAW)) return;
        if(p->seen & F_DATA) FAIL(p, IRV_ERR_DUPLICATE_KEY);
        p->token_active = false;
        p->token = 0;
        p->state = S_DATA;
        return;
    }
    p->state = S_VALUE_WS;
}

/* ---- character state machine ------------------------------------------------------ */

static void end_line(irv_parser_t* p) {
    switch(p->state) {
    case S_KEY:
        FAIL(p, IRV_ERR_SYNTAX);
    case S_VALUE_WS:
    case S_VALUE:
        end_value(p);
        break;
    case S_DATA:
        end_token(p);
        end_data(p);
        break;
    default:
        break;
    }
    if(p->state != S_DEAD) p->state = S_LINE_START;
    p->line++;
}

static void step(irv_parser_t* p, uint8_t c) {
    for(;;) {
        switch(p->state) {
        case S_LINE_START:
            if(c == '\n') {
                end_line(p);
                return;
            }
            if(c == ' ' || c == '\t' || c == '\r') return;
            if(c == '#') {
                p->state = S_COMMENT;
                return;
            }
            p->state = S_KEY;
            p->keylen = 0;
            continue; /* reprocess c as the first key character */
        case S_COMMENT:
            if(c == '\n') end_line(p);
            return;
        case S_KEY:
            if(c == ':') {
                end_key(p);
                return;
            }
            if(c <= 0x20 || c > 0x7e) FAIL(p, IRV_ERR_SYNTAX);
            if(p->keylen >= IRV_KEY_MAX) FAIL(p, IRV_ERR_KEY_TOO_LONG);
            p->key[p->keylen++] = (char)c;
            return;
        case S_VALUE_WS:
            if(c == ' ' || c == '\t') return;
            p->state = S_VALUE;
            continue;
        case S_VALUE:
            if(c == '\n') {
                end_line(p);
                return;
            }
            if(p->vallen >= IRV_VALUE_MAX)
                FAIL(p, strcmp(p->key, "name") == 0 ? IRV_ERR_NAME_TOO_LONG : IRV_ERR_VALUE_TOO_LONG);
            if((c < 0x20 && c != '\t' && c != '\r') || c > 0x7e)
                FAIL(p, strcmp(p->key, "name") == 0 ? IRV_ERR_BAD_NAME : IRV_ERR_BAD_VALUE);
            p->val[p->vallen++] = (char)c;
            return;
        case S_DATA:
            if(c == '\n') {
                end_line(p);
                return;
            }
            if(c == ' ' || c == '\t' || c == '\r') {
                end_token(p);
                return;
            }
            if(c < '0' || c > '9') FAIL(p, IRV_ERR_BAD_NUMBER);
            {
                uint64_t v = (p->token_active ? (uint64_t)p->token * 10u : 0u) + (uint64_t)(c - '0');
                if(v > UINT32_MAX) FAIL(p, IRV_ERR_NUMBER_RANGE);
                p->token = (uint32_t)v;
                p->token_active = true;
            }
            return;
        default: /* S_DEAD */
            return;
        }
    }
}

bool irv_feed(irv_parser_t* p, const uint8_t* data, size_t len) {
    if(p->res->error != IRV_OK) return false;
    if(len > IRV_MAX_FILE_BYTES - p->total) {
        fail_at(p, IRV_ERR_FILE_TOO_LARGE, p->line);
        return false;
    }
    p->total += (uint32_t)len;
    for(size_t i = 0; i < len && p->state != S_DEAD; i++) step(p, data[i]);
    return p->res->error == IRV_OK;
}

bool irv_finish(irv_parser_t* p) {
    if(p->res->error != IRV_OK) return false;
    if(p->state != S_LINE_START && p->state != S_COMMENT) end_line(p); /* no trailing newline */
    if(p->res->error != IRV_OK) return false;
    if(p->header != 2) {
        fail_at(p, IRV_ERR_BAD_HEADER, p->line);
        return false;
    }
    commit_button(p);
    return p->res->error == IRV_OK;
}

bool irv_parse_buffer(
    const uint8_t* data,
    size_t len,
    irv_result_t* res,
    irv_button_t* storage,
    size_t capacity) {
    irv_parser_t p;
    irv_init(&p, res, storage, capacity);
    irv_feed(&p, data, len);
    return irv_finish(&p);
}

bool irv_parse_reader(
    irv_read_fn read,
    void* ctx,
    irv_result_t* res,
    irv_button_t* storage,
    size_t capacity) {
    irv_parser_t p;
    uint8_t buf[128];
    irv_init(&p, res, storage, capacity);
    for(;;) {
        size_t n = read(ctx, buf, sizeof(buf));
        if(n == (size_t)-1 || n > sizeof(buf)) {
            fail_at(&p, IRV_ERR_IO, p.line);
            return false;
        }
        if(n == 0) break;
        if(!irv_feed(&p, buf, n)) return false;
    }
    return irv_finish(&p);
}

const char* irv_error_str(irv_error_t e) {
    switch(e) {
    case IRV_OK: return "ok";
    case IRV_ERR_BAD_HEADER: return "bad or missing file header";
    case IRV_ERR_UNSUPPORTED_VERSION: return "unsupported version";
    case IRV_ERR_SYNTAX: return "syntax error";
    case IRV_ERR_UNKNOWN_KEY: return "unknown key";
    case IRV_ERR_DUPLICATE_KEY: return "duplicate key";
    case IRV_ERR_ORDER: return "field out of order";
    case IRV_ERR_WRONG_FIELD_FOR_TYPE: return "field not valid for signal type";
    case IRV_ERR_KEY_TOO_LONG: return "key too long";
    case IRV_ERR_VALUE_TOO_LONG: return "value too long";
    case IRV_ERR_NAME_TOO_LONG: return "name too long";
    case IRV_ERR_BAD_NAME: return "invalid name";
    case IRV_ERR_BAD_VALUE: return "invalid value";
    case IRV_ERR_BAD_NUMBER: return "invalid number";
    case IRV_ERR_NUMBER_RANGE: return "number out of range";
    case IRV_ERR_BAD_HEX: return "invalid hex bytes";
    case IRV_ERR_TOO_MANY_TIMINGS: return "too many raw timings";
    case IRV_ERR_TOO_MANY_BUTTONS: return "too many buttons";
    case IRV_ERR_INCOMPLETE: return "incomplete button";
    case IRV_ERR_FILE_TOO_LARGE: return "file too large";
    case IRV_ERR_IO: return "read error";
    }
    return "unknown error";
}

size_t irv_describe(const irv_button_t* b, char* out, size_t out_size) {
    if(out_size == 0) return 0;
    int n;
    if(b->type == IRV_SIGNAL_PARSED) {
        n = snprintf(out, out_size, "%s", b->protocol);
    } else {
        n = snprintf(
            out,
            out_size,
            "raw %luHz, %lu timings",
            (unsigned long)b->frequency,
            (unsigned long)b->timing_count);
    }
    return n < 0 ? 0 : (size_t)n;
}
