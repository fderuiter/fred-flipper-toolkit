#include "ir_parse.h"

#include <string.h>

enum {
    SeenName = 1u << 0,
    SeenType = 1u << 1,
    SeenProtocol = 1u << 2,
    SeenAddress = 1u << 3,
    SeenCommand = 1u << 4,
    SeenFrequency = 1u << 5,
    SeenDuty = 1u << 6,
    SeenData = 1u << 7,
};

#define PARSED_FIELDS (SeenProtocol | SeenAddress | SeenCommand)
#define RAW_FIELDS    (SeenFrequency | SeenDuty | SeenData)

typedef struct {
    const char* p;
    size_t n;
} Span;

static bool span_eq(Span s, const char* lit) {
    size_t l = strlen(lit);
    return s.n == l && memcmp(s.p, lit, l) == 0;
}

/* ---- value parsers: all return false on any deviation ---- */

static bool parse_u32(Span s, uint32_t* out) {
    if(s.n == 0 || s.n > 10) return false;
    uint64_t v = 0;
    for(size_t i = 0; i < s.n; i++) {
        if(s.p[i] < '0' || s.p[i] > '9') return false;
        v = v * 10u + (uint64_t)(s.p[i] - '0');
    }
    if(v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Exactly "XX XX XX XX". */
static bool parse_hex4(Span s, uint8_t out[4]) {
    if(s.n != 11) return false;
    for(size_t b = 0; b < 4; b++) {
        int hi = hex_val(s.p[b * 3]);
        int lo = hex_val(s.p[b * 3 + 1]);
        if(hi < 0 || lo < 0) return false;
        if(b < 3 && s.p[b * 3 + 2] != ' ') return false;
        out[b] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* Decimal in (0, 1]; at most 1 integer digit and 9 fraction digits. No strtod. */
static bool parse_duty(Span s, float* out) {
    if(s.n == 0 || s.n > 11) return false;
    size_t i = 0;
    if(s.p[i] != '0' && s.p[i] != '1') return false;
    uint32_t ip = (uint32_t)(s.p[i++] - '0');
    uint64_t frac = 0, scale = 1;
    if(i < s.n) {
        if(s.p[i++] != '.') return false;
        if(i == s.n) return false;
        if(s.n - i > 9) return false;
        for(; i < s.n; i++) {
            if(s.p[i] < '0' || s.p[i] > '9') return false;
            frac = frac * 10u + (uint64_t)(s.p[i] - '0');
            scale *= 10u;
        }
    }
    if(ip == 1 && frac != 0) return false;
    if(ip == 0 && frac == 0) return false;
    *out = (float)ip + (float)((double)frac / (double)scale);
    return true;
}

static bool is_printable_ascii(Span s) {
    for(size_t i = 0; i < s.n; i++) {
        if((unsigned char)s.p[i] < 0x20 || (unsigned char)s.p[i] > 0x7E) return false;
    }
    return true;
}

static bool copy_text(Span s, char* dst, size_t max_len) {
    if(s.n == 0 || s.n > max_len || !is_printable_ascii(s)) return false;
    memcpy(dst, s.p, s.n);
    dst[s.n] = '\0';
    return true;
}

/* Validates and counts timings without storing them. */
static IrParseError count_timings(Span s, uint32_t* count) {
    size_t i = 0;
    uint32_t n = 0;
    while(i < s.n) {
        while(i < s.n && s.p[i] == ' ') i++;
        if(i == s.n) break;
        size_t start = i;
        while(i < s.n && s.p[i] != ' ') i++;
        Span tok = {s.p + start, i - start};
        uint32_t v;
        if(tok.n > 7 || !parse_u32(tok, &v) || v == 0 || v > IR_MAX_TIMING_US)
            return IrParseErrBadValue;
        if(n >= IR_MAX_RAW_TIMINGS) return IrParseErrTooManyTimings;
        n++;
    }
    if(n == 0) return IrParseErrBadValue;
    *count = n;
    return IrParseOk;
}

/* ---- parser state ---- */

typedef struct {
    IrFile* out;
    IrSignal cur;
    uint32_t seen;
    bool in_signal;
} Ctx;

static void ctx_begin_signal(Ctx* c) {
    memset(&c->cur, 0, sizeof(c->cur));
    c->seen = 0;
    c->in_signal = true;
}

static IrParseError ctx_finish_signal(Ctx* c) {
    if(!c->in_signal) return IrParseOk;
    c->in_signal = false;
    if(!(c->seen & SeenName) || !(c->seen & SeenType)) return IrParseErrIncomplete;
    uint32_t need = c->cur.type == IrSignalParsed ? PARSED_FIELDS : RAW_FIELDS;
    uint32_t other = c->cur.type == IrSignalParsed ? RAW_FIELDS : PARSED_FIELDS;
    if((c->seen & need) != need) return IrParseErrIncomplete;
    if(c->seen & other) return IrParseErrBadValue;
    if(c->out->count >= IR_MAX_SIGNALS) return IrParseErrTooManySignals;
    c->out->signals[c->out->count++] = c->cur;
    return IrParseOk;
}

static IrParseError ctx_set_field(Ctx* c, Span key, Span val) {
    uint32_t bit;
    if(span_eq(key, "name")) bit = SeenName;
    else if(span_eq(key, "type")) bit = SeenType;
    else if(span_eq(key, "protocol")) bit = SeenProtocol;
    else if(span_eq(key, "address")) bit = SeenAddress;
    else if(span_eq(key, "command")) bit = SeenCommand;
    else if(span_eq(key, "frequency")) bit = SeenFrequency;
    else if(span_eq(key, "duty_cycle")) bit = SeenDuty;
    else if(span_eq(key, "data")) bit = SeenData;
    else return IrParseErrUnknownKey;

    if(!c->in_signal) ctx_begin_signal(c);
    if(c->seen & bit) return IrParseErrDuplicateKey;

    IrSignal* s = &c->cur;
    bool ok = false;
    switch(bit) {
    case SeenName:
        ok = copy_text(val, s->name, IR_MAX_NAME_LEN);
        break;
    case SeenType:
        if(span_eq(val, "parsed")) {
            s->type = IrSignalParsed;
            ok = true;
        } else if(span_eq(val, "raw")) {
            s->type = IrSignalRaw;
            ok = true;
        }
        break;
    case SeenProtocol:
        ok = copy_text(val, s->protocol, IR_MAX_PROTOCOL_LEN);
        break;
    case SeenAddress:
        ok = parse_hex4(val, s->address);
        break;
    case SeenCommand:
        ok = parse_hex4(val, s->command);
        break;
    case SeenFrequency:
        ok = parse_u32(val, &s->frequency) && s->frequency >= IR_MIN_FREQUENCY &&
             s->frequency <= IR_MAX_FREQUENCY;
        break;
    case SeenDuty:
        ok = parse_duty(val, &s->duty_cycle);
        break;
    case SeenData: {
        IrParseError e = count_timings(val, &s->timing_count);
        if(e != IrParseOk) return e;
        ok = true;
        break;
    }
    }
    if(!ok) return IrParseErrBadValue;
    c->seen |= bit;
    return IrParseOk;
}

/* Splits "key: value". Value has leading spaces trimmed and trailing spaces removed. */
static bool split_kv(Span line, Span* key, Span* val) {
    size_t i = 0;
    while(i < line.n && line.p[i] != ':') i++;
    if(i == 0 || i >= line.n) return false;
    key->p = line.p;
    key->n = i;
    i++;
    while(i < line.n && line.p[i] == ' ') i++;
    size_t end = line.n;
    while(end > i && line.p[end - 1] == ' ') end--;
    val->p = line.p + i;
    val->n = end - i;
    return true;
}

IrParseResult ir_parse(const char* buf, size_t len, IrFile* out) {
    IrParseResult r = {IrParseOk, 0};
    if(!out) {
        r.error = IrParseErrNullArg;
        return r;
    }
    out->count = 0;
    if(!buf && len) {
        r.error = IrParseErrNullArg;
        return r;
    }
    if(len == 0) {
        r.error = IrParseErrEmpty;
        return r;
    }
    if(len > IR_MAX_FILE_BYTES) {
        r.error = IrParseErrFileTooLarge;
        return r;
    }

    Ctx c;
    memset(&c, 0, sizeof(c));
    c.out = out;

    size_t pos = 0, lineno = 0;
    int header_state = 0; /* 0: want Filetype, 1: want Version, 2: body */
    bool last_line_unterminated = false;

    while(pos < len) {
        size_t start = pos;
        while(pos < len && buf[pos] != '\n') pos++;
        last_line_unterminated = (pos == len);
        size_t end = pos;
        if(pos < len) pos++; /* skip '\n' */
        lineno++;
        if(end > start && buf[end - 1] == '\r') end--;
        if(end - start > IR_MAX_LINE_LEN) {
            r.error = IrParseErrLineTooLong;
            r.line = lineno;
            return r;
        }
        Span line = {buf + start, end - start};
        IrParseError e = IrParseOk;

        if(line.n == 0) {
            if(header_state < 2) e = IrParseErrBadHeader; /* header must come first */
        } else if(header_state < 2) {
            Span k, v;
            if(!split_kv(line, &k, &v)) {
                e = IrParseErrBadHeader;
            } else if(header_state == 0) {
                if(!span_eq(k, "Filetype") || !span_eq(v, "IR signals file"))
                    e = IrParseErrBadHeader;
            } else {
                uint32_t ver;
                if(!span_eq(k, "Version") || !parse_u32(v, &ver) || ver != 1)
                    e = IrParseErrBadVersion;
            }
            if(e == IrParseOk) header_state++;
        } else if(line.p[0] == '#') {
            e = ctx_finish_signal(&c);
        } else {
            Span k, v;
            if(!split_kv(line, &k, &v)) e = IrParseErrSyntax;
            else e = ctx_set_field(&c, k, v);
        }

        if(e != IrParseOk) {
            /* A bad value on an unterminated last line is reported as truncation. */
            if(last_line_unterminated && (e == IrParseErrBadValue || e == IrParseErrSyntax) &&
               header_state == 2)
                e = IrParseErrTruncated;
            r.error = e;
            r.line = lineno;
            return r;
        }
    }

    if(header_state < 2) {
        r.error = header_state == 0 ? IrParseErrBadHeader : IrParseErrBadVersion;
        r.line = lineno;
        return r;
    }
    IrParseError e = ctx_finish_signal(&c);
    if(e != IrParseOk) {
        r.error = (e == IrParseErrIncomplete && last_line_unterminated) ? IrParseErrTruncated : e;
        r.line = lineno;
    }
    return r;
}

const char* ir_parse_error_str(IrParseError e) {
    switch(e) {
    case IrParseOk: return "ok";
    case IrParseErrNullArg: return "null argument";
    case IrParseErrEmpty: return "empty file";
    case IrParseErrFileTooLarge: return "file too large";
    case IrParseErrLineTooLong: return "line too long";
    case IrParseErrBadHeader: return "not an IR signals file";
    case IrParseErrBadVersion: return "unsupported version";
    case IrParseErrSyntax: return "syntax error";
    case IrParseErrUnknownKey: return "unsupported key";
    case IrParseErrDuplicateKey: return "duplicate key";
    case IrParseErrBadValue: return "invalid value";
    case IrParseErrTooManyTimings: return "too many timings";
    case IrParseErrTooManySignals: return "too many signals";
    case IrParseErrTruncated: return "truncated file";
    case IrParseErrIncomplete: return "incomplete signal";
    }
    return "unknown error";
}
