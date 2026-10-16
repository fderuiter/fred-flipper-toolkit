#include "ir_parser.h"

#include <string.h>

enum {
    KeyName = 1u << 0,
    KeyType = 1u << 1,
    KeyProtocol = 1u << 2,
    KeyAddress = 1u << 3,
    KeyCommand = 1u << 4,
    KeyFrequency = 1u << 5,
    KeyDuty = 1u << 6,
    KeyData = 1u << 7,
};
#define PARSED_KEYS (KeyProtocol | KeyAddress | KeyCommand)
#define RAW_KEYS    (KeyFrequency | KeyDuty | KeyData)

typedef struct {
    IrFile* out;
    char line[IR_MAX_LINE];
    size_t len;
    bool overflow;
    size_t line_no; /* 1-based number of the line being read */
    size_t sig_line;
    int header; /* 0: want Filetype, 1: want Version, 2: body */
    bool open;  /* out->signals[out->count] is being filled */
    unsigned seen;
    /* streaming "data:" state */
    bool in_data;
    bool tok_active;
    uint32_t tok_val;
    IrError err;
    size_t err_line;
} Parser;

static bool fail(Parser* p, IrError e, size_t line) {
    if(p->err == IrErrNone) {
        p->err = e;
        p->err_line = line;
    }
    return false;
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly "XX XX XX XX". */
static bool parse_hex4(const char* v, size_t n, uint8_t out[4]) {
    if(n != 11) return false;
    for(size_t i = 0; i < 4; i++) {
        int hi = hex_val(v[i * 3]);
        int lo = hex_val(v[i * 3 + 1]);
        if(hi < 0 || lo < 0) return false;
        if(i < 3 && v[i * 3 + 2] != ' ') return false;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return true;
}

static bool parse_uint(const char* v, size_t n, uint32_t* out) {
    if(n == 0 || n > 10) return false;
    uint64_t acc = 0;
    for(size_t i = 0; i < n; i++) {
        if(!is_digit(v[i])) return false;
        acc = acc * 10 + (uint64_t)(v[i] - '0');
    }
    if(acc > UINT32_MAX) return false;
    *out = (uint32_t)acc;
    return true;
}

/* "0.33", "1", "0.330000" -> permille, must be in (0, 1]. */
static bool parse_duty(const char* v, size_t n, uint16_t* out) {
    if(n == 0 || (v[0] != '0' && v[0] != '1')) return false;
    uint32_t ppm = (uint32_t)(v[0] - '0') * 1000000u;
    if(n > 1) {
        if(v[1] != '.' || n == 2) return false;
        uint32_t scale = 100000u;
        for(size_t i = 2; i < n; i++) {
            if(!is_digit(v[i])) return false;
            if(scale) {
                ppm += (uint32_t)(v[i] - '0') * scale;
                scale /= 10;
            }
        }
    }
    if(ppm > 1000000u || ppm / 1000u == 0) return false;
    *out = (uint16_t)(ppm / 1000u);
    return true;
}

static bool commit(Parser* p) {
    if(!p->open) return true;
    IrSignal* s = &p->out->signals[p->out->count];
    unsigned need;
    unsigned other;
    if(s->type == IrTypeParsed) {
        need = PARSED_KEYS;
        other = RAW_KEYS;
    } else {
        need = RAW_KEYS;
        other = PARSED_KEYS;
    }
    if(!(p->seen & KeyType) || (p->seen & need) != need || (p->seen & other)) {
        return fail(p, IrErrIncomplete, p->sig_line);
    }
    if(s->type == IrTypeRaw && s->timing_count == 0) {
        return fail(p, IrErrIncomplete, p->sig_line);
    }
    p->out->count++;
    p->open = false;
    return true;
}

static bool handle_key(Parser* p, const char* key, size_t klen, const char* v, size_t vlen) {
    IrFile* out = p->out;
#define KEY_IS(lit) (klen == sizeof(lit) - 1 && memcmp(key, lit, klen) == 0)

    if(KEY_IS("name")) {
        if(!commit(p)) return false;
        if(out->count >= IR_MAX_SIGNALS) return fail(p, IrErrTooManySignals, p->line_no);
        if(vlen == 0) return fail(p, IrErrBadValue, p->line_no);
        IrSignal* s = &out->signals[out->count];
        memset(s, 0, sizeof(*s));
        size_t n = vlen < IR_NAME_SIZE - 1 ? vlen : IR_NAME_SIZE - 1;
        memcpy(s->name, v, n);
        s->name_truncated = vlen > n;
        p->open = true;
        p->seen = KeyName;
        p->sig_line = p->line_no;
        return true;
    }

    unsigned bit;
    if(KEY_IS("type")) bit = KeyType;
    else if(KEY_IS("protocol")) bit = KeyProtocol;
    else if(KEY_IS("address")) bit = KeyAddress;
    else if(KEY_IS("command")) bit = KeyCommand;
    else if(KEY_IS("frequency")) bit = KeyFrequency;
    else if(KEY_IS("duty_cycle")) bit = KeyDuty;
    else if(KEY_IS("data")) bit = KeyData; /* only reached for "data" without streaming, i.e. never */
    else return fail(p, IrErrUnknownKey, p->line_no);
#undef KEY_IS

    if(!p->open) return fail(p, IrErrIncomplete, p->line_no); /* key before any name */
    if(p->seen & bit) return fail(p, IrErrDuplicateKey, p->line_no);
    IrSignal* s = &out->signals[out->count];

    switch(bit) {
    case KeyType:
        if(vlen == 6 && memcmp(v, "parsed", 6) == 0) s->type = IrTypeParsed;
        else if(vlen == 3 && memcmp(v, "raw", 3) == 0) s->type = IrTypeRaw;
        else return fail(p, IrErrBadType, p->line_no);
        break;
    case KeyProtocol:
        if(vlen == 0 || vlen >= IR_PROTOCOL_SIZE) return fail(p, IrErrBadValue, p->line_no);
        for(size_t i = 0; i < vlen; i++) {
            char c = v[i];
            bool ok = is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                      c == '_' || c == '-';
            if(!ok) return fail(p, IrErrBadValue, p->line_no);
        }
        memcpy(s->protocol, v, vlen);
        s->protocol[vlen] = '\0';
        break;
    case KeyAddress:
        if(!parse_hex4(v, vlen, s->address)) return fail(p, IrErrBadHex, p->line_no);
        break;
    case KeyCommand:
        if(!parse_hex4(v, vlen, s->command)) return fail(p, IrErrBadHex, p->line_no);
        break;
    case KeyFrequency:
        if(!parse_uint(v, vlen, &s->frequency) || s->frequency < IR_MIN_FREQUENCY ||
           s->frequency > IR_MAX_FREQUENCY)
            return fail(p, IrErrBadValue, p->line_no);
        break;
    case KeyDuty:
        if(!parse_duty(v, vlen, &s->duty_permille)) return fail(p, IrErrBadValue, p->line_no);
        break;
    default:
        return fail(p, IrErrSyntax, p->line_no);
    }
    p->seen |= bit;
    return true;
}

static bool process_line(Parser* p) {
    char* l = p->line;
    size_t n = p->len;
    if(n && l[n - 1] == '\r') n--;

    for(size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)l[i];
        if((c < 0x20 && c != '\t') || c == 0x7f) return fail(p, IrErrNonPrintable, p->line_no);
    }
    if(n == 0 || l[0] == '#') return true;

    size_t colon = 0;
    while(colon < n && l[colon] != ':') colon++;
    if(colon == n || colon == 0) return fail(p, IrErrSyntax, p->line_no);

    const char* v = l + colon + 1;
    size_t vlen = n - colon - 1;
    while(vlen && (*v == ' ' || *v == '\t')) {
        v++;
        vlen--;
    }
    while(vlen && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t')) vlen--;

    if(p->header == 0) {
        if(colon != 8 || memcmp(l, "Filetype", 8) != 0 || vlen != 15 ||
           memcmp(v, "IR signals file", 15) != 0)
            return fail(p, IrErrBadHeader, p->line_no);
        p->header = 1;
        return true;
    }
    if(p->header == 1) {
        if(colon != 7 || memcmp(l, "Version", 7) != 0) return fail(p, IrErrBadHeader, p->line_no);
        uint32_t ver;
        if(!parse_uint(v, vlen, &ver)) return fail(p, IrErrBadHeader, p->line_no);
        if(ver != 1) return fail(p, IrErrBadVersion, p->line_no);
        p->header = 2;
        return true;
    }
    return handle_key(p, l, colon, v, vlen);
}

static bool begin_data(Parser* p) {
    IrSignal* s = &p->out->signals[p->out->count];
    if(!p->open || !(p->seen & KeyType) || s->type != IrTypeRaw)
        return fail(p, IrErrIncomplete, p->line_no);
    if(p->seen & KeyData) return fail(p, IrErrDuplicateKey, p->line_no);
    p->seen |= KeyData;
    p->in_data = true;
    p->tok_active = false;
    return true;
}

static bool end_token(Parser* p) {
    if(!p->tok_active) return true;
    p->tok_active = false;
    IrSignal* s = &p->out->signals[p->out->count];
    if(s->timing_count >= IR_MAX_TIMINGS) return fail(p, IrErrTooManyTimings, p->line_no);
    s->timing_count++;
    return true;
}

static bool feed(Parser* p, char c) {
    if(p->in_data) {
        if(c == '\n') {
            if(!end_token(p)) return false;
            p->in_data = false;
            p->len = 0;
            p->line_no++;
        } else if(c == ' ' || c == '\t' || c == '\r') {
            if(!end_token(p)) return false;
        } else if(is_digit(c)) {
            uint32_t d = (uint32_t)(c - '0');
            if(!p->tok_active) {
                p->tok_active = true;
                p->tok_val = 0;
            }
            if(p->tok_val > (UINT32_MAX - d) / 10u) return fail(p, IrErrBadTiming, p->line_no);
            p->tok_val = p->tok_val * 10u + d;
        } else {
            return fail(p, IrErrBadTiming, p->line_no);
        }
        return true;
    }

    if(c == '\n') {
        bool ok;
        if(p->overflow) ok = fail(p, IrErrLineTooLong, p->line_no);
        else ok = process_line(p);
        p->len = 0;
        p->overflow = false;
        p->line_no++;
        return ok;
    }
    if(p->len < IR_MAX_LINE) p->line[p->len++] = c;
    else p->overflow = true;

    if(p->header == 2 && !p->overflow && p->len == 5 && memcmp(p->line, "data:", 5) == 0) {
        return begin_data(p);
    }
    return true;
}

typedef struct {
    const uint8_t* data;
    size_t size;
    size_t pos;
} BufSrc;

static size_t buf_read(void* ctx, uint8_t* dst, size_t max) {
    BufSrc* b = ctx;
    size_t n = b->size - b->pos;
    if(n > max) n = max;
    if(n) memcpy(dst, b->data + b->pos, n);
    b->pos += n;
    return n;
}

bool ir_parse(IrReadFn read, void* ctx, IrFile* out) {
    memset(out, 0, sizeof(*out));
    Parser p;
    memset(&p, 0, sizeof(p));
    p.out = out;
    p.line_no = 1;

    uint8_t chunk[128];
    bool ok = true;
    bool any = false;
    while(ok) {
        size_t n = read(ctx, chunk, sizeof(chunk));
        if(n == (size_t)-1) {
            ok = fail(&p, IrErrRead, 0);
            break;
        }
        if(n == 0) break;
        if(n > sizeof(chunk)) { /* misbehaving callback */
            ok = fail(&p, IrErrRead, 0);
            break;
        }
        any = true;
        for(size_t i = 0; ok && i < n; i++) ok = feed(&p, (char)chunk[i]);
    }

    if(ok) {
        if(!any || p.header == 0) {
            ok = fail(&p, IrErrEmpty, 0);
        } else if(p.in_data || p.len > 0) {
            ok = fail(&p, IrErrTruncated, p.line_no);
        } else if(p.header == 1) {
            ok = fail(&p, IrErrBadHeader, p.line_no);
        } else {
            ok = commit(&p);
        }
    }

    if(!ok) {
        out->count = 0;
        out->error = p.err;
        out->error_line = p.err_line;
    }
    return ok;
}

bool ir_parse_buffer(const uint8_t* data, size_t size, IrFile* out) {
    BufSrc b = {data, size, 0};
    return ir_parse(buf_read, &b, out);
}

const char* ir_error_str(IrError e) {
    switch(e) {
    case IrErrNone: return "ok";
    case IrErrEmpty: return "empty file";
    case IrErrBadHeader: return "not an IR signals file";
    case IrErrBadVersion: return "unsupported version";
    case IrErrLineTooLong: return "line too long";
    case IrErrNonPrintable: return "control character in file";
    case IrErrSyntax: return "malformed line";
    case IrErrUnknownKey: return "unsupported key";
    case IrErrDuplicateKey: return "duplicate key";
    case IrErrBadType: return "bad signal type";
    case IrErrBadValue: return "bad value";
    case IrErrBadHex: return "bad address/command";
    case IrErrBadTiming: return "bad raw timing";
    case IrErrTooManyTimings: return "too many raw timings";
    case IrErrIncomplete: return "incomplete signal";
    case IrErrTooManySignals: return "too many signals";
    case IrErrTruncated: return "file truncated";
    case IrErrRead: return "read error";
    }
    return "unknown error";
}
