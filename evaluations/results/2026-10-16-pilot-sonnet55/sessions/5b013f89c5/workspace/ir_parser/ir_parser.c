#include "ir_parser.h"

#include <string.h>

enum {
    F_NAME = 1 << 0,
    F_TYPE = 1 << 1,
    F_PROTO = 1 << 2,
    F_ADDR = 1 << 3,
    F_CMD = 1 << 4,
    F_FREQ = 1 << 5,
    F_DUTY = 1 << 6,
    F_DATA = 1 << 7,
};
#define PARSED_FIELDS (F_NAME | F_TYPE | F_PROTO | F_ADDR | F_CMD)
#define RAW_FIELDS    (F_NAME | F_TYPE | F_FREQ | F_DUTY | F_DATA)

typedef enum { StHeaderFiletype, StHeaderVersion, StBody } Stage;
typedef enum { PhLineStart, PhComment, PhKey, PhValueSpace, PhValue, PhData } Phase;

typedef struct {
    IrRemote* out;
    Stage stage;
    Phase phase;
    size_t line;
    int in_signal;
    unsigned fields;
    IrSignal* cur;
    int key_id; /* F_* of the key being read, 0 for Filetype/Version */
    char key[16];
    size_t key_len;
    char val[IR_MAX_VALUE_LEN + 1];
    size_t val_len;
    /* streaming `data` tokenizer */
    uint64_t tok;
    size_t tok_digits;
} State;

static int fail(State* s, IrError e) {
    s->out->error = e;
    s->out->error_line = s->line;
    return 0;
}

static int printable(uint8_t c) {
    return c >= 0x20 && c <= 0x7e;
}

static int parse_u32(const char* p, size_t len, uint32_t* v) {
    if(len == 0 || len > 10) return 0;
    uint64_t acc = 0;
    for(size_t i = 0; i < len; i++) {
        if(p[i] < '0' || p[i] > '9') return 0;
        acc = acc * 10 + (uint64_t)(p[i] - '0');
    }
    if(acc > UINT32_MAX) return 0;
    *v = (uint32_t)acc;
    return 1;
}

/* "0.33", "1", "1.0", "0.330000" -> ppm. Digits beyond 6 decimals must be digits and are truncated. */
static int parse_duty(const char* p, size_t len, uint32_t* ppm) {
    size_t i = 0;
    uint32_t ip = 0, frac = 0, scale = 100000;
    size_t int_digits = 0;
    while(i < len && p[i] >= '0' && p[i] <= '9') {
        if(++int_digits > 1) return 0;
        ip = (uint32_t)(p[i] - '0');
        i++;
    }
    if(int_digits == 0) return 0;
    if(i < len) {
        if(p[i++] != '.' || i == len) return 0;
        for(; i < len; i++) {
            if(p[i] < '0' || p[i] > '9') return 0;
            if(scale) {
                frac += (uint32_t)(p[i] - '0') * scale;
                scale /= 10;
            }
        }
    }
    uint32_t total = ip * 1000000u + frac;
    if(total == 0 || total > 1000000u) return 0;
    *ppm = total;
    return 1;
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly "XX XX XX XX". */
static int parse_hex4(const char* p, size_t len, uint8_t out[4]) {
    if(len != 11) return 0;
    for(size_t i = 0; i < 4; i++) {
        int hi = hex_nibble(p[i * 3]), lo = hex_nibble(p[i * 3 + 1]);
        if(hi < 0 || lo < 0) return 0;
        if(i < 3 && p[i * 3 + 2] != ' ') return 0;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return 1;
}

static int finalize_signal(State* s) {
    if(!s->in_signal) return 1;
    s->in_signal = 0;
    if(!(s->fields & F_TYPE)) return fail(s, IrErrIncompleteSignal);
    unsigned want = (s->cur->type == IrSignalParsed) ? PARSED_FIELDS : RAW_FIELDS;
    if(s->fields != want) return fail(s, IrErrIncompleteSignal);
    s->out->count++;
    return 1;
}

static int end_data_token(State* s) {
    if(s->tok_digits == 0) return 1;
    if(s->tok == 0) return fail(s, IrErrBadValue);
    if(s->cur->timing_count >= IR_MAX_TIMINGS) return fail(s, IrErrTooManyTimings);
    s->cur->timing_count++;
    s->tok = 0;
    s->tok_digits = 0;
    return 1;
}

static int start_signal(State* s) {
    if(s->out->count >= IR_MAX_SIGNALS) return fail(s, IrErrTooManySignals);
    s->cur = &s->out->signals[s->out->count];
    memset(s->cur, 0, sizeof(*s->cur));
    s->in_signal = 1;
    s->fields = 0;
    return 1;
}

static int key_lookup(const char* k) {
    static const struct {
        const char* n;
        int id;
    } tab[] = {
        {"name", F_NAME},
        {"type", F_TYPE},
        {"protocol", F_PROTO},
        {"address", F_ADDR},
        {"command", F_CMD},
        {"frequency", F_FREQ},
        {"duty_cycle", F_DUTY},
        {"data", F_DATA},
    };
    for(size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if(strcmp(k, tab[i].n) == 0) return tab[i].id;
    }
    return 0;
}

/* Called when the ':' after a key is seen. */
static int key_done(State* s) {
    s->key[s->key_len] = 0;
    s->val_len = 0;
    if(s->stage == StHeaderFiletype || s->stage == StHeaderVersion) {
        const char* want = (s->stage == StHeaderFiletype) ? "Filetype" : "Version";
        if(strcmp(s->key, want) != 0) return fail(s, IrErrBadHeader);
        s->key_id = 0;
        s->phase = PhValueSpace;
        return 1;
    }
    int id = key_lookup(s->key);
    if(!id) return fail(s, IrErrUnknownKey);
    if(!s->in_signal && !start_signal(s)) return 0;
    if(s->fields & (unsigned)id) return fail(s, IrErrDuplicateKey);
    s->fields |= (unsigned)id;
    s->key_id = id;
    s->tok = 0;
    s->tok_digits = 0;
    s->phase = (id == F_DATA) ? PhData : PhValueSpace;
    return 1;
}

static int value_done(State* s) {
    s->val[s->val_len] = 0;
    while(s->val_len && (s->val[s->val_len - 1] == ' ' || s->val[s->val_len - 1] == '\t')) {
        s->val[--s->val_len] = 0;
    }
    const char* v = s->val;
    size_t n = s->val_len;
    IrSignal* g = s->cur;

    if(s->stage == StHeaderFiletype) {
        if(strcmp(v, "IR signals file") != 0) return fail(s, IrErrBadHeader);
        s->stage = StHeaderVersion;
        return 1;
    }
    if(s->stage == StHeaderVersion) {
        uint32_t ver;
        if(!parse_u32(v, n, &ver)) return fail(s, IrErrBadHeader);
        if(ver != 1) return fail(s, IrErrUnsupportedVersion);
        s->stage = StBody;
        return 1;
    }

    switch(s->key_id) {
    case F_NAME:
        if(n == 0) return fail(s, IrErrBadValue);
        memcpy(g->name, v, n + 1); /* n <= IR_MAX_NAME_LEN checked below */
        return 1;
    case F_TYPE:
        if(strcmp(v, "parsed") == 0) g->type = IrSignalParsed;
        else if(strcmp(v, "raw") == 0) g->type = IrSignalRaw;
        else return fail(s, IrErrBadValue);
        return 1;
    case F_PROTO:
        if(n == 0 || n > IR_MAX_PROTOCOL_LEN) return fail(s, IrErrBadValue);
        for(size_t i = 0; i < n; i++) {
            if(v[i] == ' ') return fail(s, IrErrBadValue);
        }
        memcpy(g->protocol, v, n + 1);
        return 1;
    case F_ADDR:
        return parse_hex4(v, n, g->address) ? 1 : fail(s, IrErrBadValue);
    case F_CMD:
        return parse_hex4(v, n, g->command) ? 1 : fail(s, IrErrBadValue);
    case F_FREQ:
        if(!parse_u32(v, n, &g->frequency) || g->frequency < IR_MIN_FREQUENCY ||
           g->frequency > IR_MAX_FREQUENCY) {
            return fail(s, IrErrBadValue);
        }
        return 1;
    case F_DUTY:
        return parse_duty(v, n, &g->duty_ppm) ? 1 : fail(s, IrErrBadValue);
    default:
        return fail(s, IrErrUnknownKey);
    }
}

static int end_line(State* s) {
    switch(s->phase) {
    case PhLineStart:
    case PhComment:
        break;
    case PhKey:
        if(s->key_len == 0) break; /* blank line */
        return fail(s, IrErrMalformedLine); /* key without ':' */
    case PhValueSpace:
    case PhValue:
        if(!value_done(s)) return 0;
        break;
    case PhData:
        if(!end_data_token(s)) return 0;
        if(s->cur->timing_count == 0) return fail(s, IrErrBadValue);
        break;
    }
    s->phase = PhLineStart;
    return 1;
}

static int feed(State* s, uint8_t c) {
    if(c == '\r') return 1; /* CRLF tolerated */
    if(c == '\n') {
        int ok = end_line(s);
        s->line++;
        return ok;
    }
    switch(s->phase) {
    case PhLineStart:
        if(c == '#') {
            if(s->stage == StBody && !finalize_signal(s)) return 0;
            s->phase = PhComment;
            return 1;
        }
        if(c == ' ' || c == '\t') return 1;
        s->key_len = 0;
        s->phase = PhKey;
        return feed(s, c);
    case PhComment:
        return 1;
    case PhKey:
        if(c == ':') return key_done(s);
        if(!printable(c)) return fail(s, IrErrMalformedLine);
        if(s->key_len >= sizeof(s->key) - 1) {
            return fail(s, s->stage == StBody ? IrErrUnknownKey : IrErrBadHeader);
        }
        s->key[s->key_len++] = (char)c;
        return 1;
    case PhValueSpace:
        if(c == ' ' || c == '\t') return 1;
        s->phase = PhValue;
        return feed(s, c);
    case PhValue: {
        if(!printable(c)) return fail(s, IrErrMalformedLine);
        /* name has its own tighter limit; others are bounded by IR_MAX_VALUE_LEN */
        size_t limit = (s->key_id == F_NAME) ? IR_MAX_NAME_LEN : IR_MAX_VALUE_LEN;
        if(s->val_len >= limit) return fail(s, IrErrLineTooLong);
        s->val[s->val_len++] = (char)c;
        return 1;
    }
    case PhData:
        if(c == ' ' || c == '\t') return end_data_token(s);
        if(c < '0' || c > '9') {
            return fail(s, printable(c) ? IrErrBadValue : IrErrMalformedLine);
        }
        s->tok = s->tok * 10 + (uint64_t)(c - '0');
        if(++s->tok_digits > 10 || s->tok > UINT32_MAX) return fail(s, IrErrBadValue);
        return 1;
    }
    return fail(s, IrErrMalformedLine);
}

IrError ir_parse_stream(IrReadFn read, void* ctx, IrRemote* out) {
    memset(out, 0, sizeof(*out));
    State s;
    memset(&s, 0, sizeof(s));
    s.out = out;
    s.line = 1;

    uint8_t chunk[128];
    size_t total = 0;
    for(;;) {
        size_t n = read(ctx, chunk, sizeof(chunk));
        if(n == 0) break;
        if(n > sizeof(chunk)) { /* includes IR_READ_ERROR and a misbehaving callback */
            fail(&s, IrErrIo);
            return out->error;
        }
        if(n > IR_MAX_INPUT_BYTES - total) {
            fail(&s, IrErrInputTooLarge);
            return out->error;
        }
        total += n;
        for(size_t i = 0; i < n; i++) {
            if(!feed(&s, chunk[i])) return out->error;
        }
    }

    /* EOF: treat an unterminated last line like a terminated one. */
    if(!end_line(&s)) return out->error;
    if(s.stage != StBody) {
        fail(&s, IrErrBadHeader);
        return out->error;
    }
    if(!finalize_signal(&s)) return out->error;
    out->error_line = 0;
    return IrOk;
}

typedef struct {
    const uint8_t* p;
    size_t left;
} MemSrc;

static size_t mem_read(void* ctx, uint8_t* buf, size_t cap) {
    MemSrc* m = ctx;
    size_t n = m->left < cap ? m->left : cap;
    if(n) memcpy(buf, m->p, n);
    m->p += n;
    m->left -= n;
    return n;
}

IrError ir_parse_buffer(const uint8_t* data, size_t len, IrRemote* out) {
    MemSrc m = {data, data ? len : 0};
    return ir_parse_stream(mem_read, &m, out);
}

const char* ir_error_str(IrError e) {
    switch(e) {
    case IrOk: return "ok";
    case IrErrIo: return "read error";
    case IrErrInputTooLarge: return "file too large";
    case IrErrBadHeader: return "not an IR signals file";
    case IrErrUnsupportedVersion: return "unsupported version";
    case IrErrMalformedLine: return "malformed line";
    case IrErrLineTooLong: return "line too long";
    case IrErrUnknownKey: return "unsupported key";
    case IrErrDuplicateKey: return "duplicate key";
    case IrErrBadValue: return "bad value";
    case IrErrTooManyTimings: return "too many timings";
    case IrErrTooManySignals: return "too many buttons";
    case IrErrIncompleteSignal: return "incomplete signal";
    }
    return "unknown error";
}
