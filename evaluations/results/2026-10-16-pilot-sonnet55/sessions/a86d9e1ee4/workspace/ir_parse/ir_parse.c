#include "ir_parse.h"

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

#define SEEN_PARSED_ONLY (SeenProtocol | SeenAddress | SeenCommand)
#define SEEN_RAW_ONLY    (SeenFrequency | SeenDuty | SeenData)

typedef struct {
    IrReadFn read;
    void* ctx;
    uint8_t chunk[256];
    size_t pos, len;
    size_t total;
    bool eof;
    IrParseError err;
} Reader;

/* Returns the next byte in *c, true; false on EOF or error (check r->err). */
static bool reader_next(Reader* r, uint8_t* c) {
    if(r->pos == r->len) {
        if(r->eof || r->err != IrParseOk) return false;
        int32_t n = r->read(r->ctx, r->chunk, sizeof(r->chunk));
        if(n < 0 || (size_t)n > sizeof(r->chunk)) {
            r->err = IrParseErrRead;
            return false;
        }
        if(n == 0) {
            r->eof = true;
            return false;
        }
        if((size_t)n > IR_MAX_FILE_BYTES - r->total) {
            r->err = IrParseErrFileTooLarge;
            return false;
        }
        r->total += (size_t)n;
        r->pos = 0;
        r->len = (size_t)n;
    }
    *c = r->chunk[r->pos++];
    return true;
}

typedef enum {
    LineOk,
    LineEof,
    LineError, /* r->err set */
} LineStatus;

/* Reads one line into buf (NUL-terminated, without \n / trailing \r). */
static LineStatus read_line(Reader* r, char* buf, size_t* out_len) {
    size_t n = 0;
    uint8_t c;
    bool any = false;
    while(reader_next(r, &c)) {
        any = true;
        if(c == '\n') goto done;
        if(n >= IR_MAX_LINE) {
            r->err = IrParseErrLineTooLong;
            return LineError;
        }
        buf[n++] = (char)c;
    }
    if(r->err != IrParseOk) return LineError;
    if(!any) return LineEof;
done:
    if(n > 0 && buf[n - 1] == '\r') n--;
    buf[n] = '\0';
    *out_len = n;
    return LineOk;
}

static bool is_ws(char c) {
    return c == ' ' || c == '\t';
}

/* Parses a decimal u32 from [s, end) fully (digits only, <= 10 digits). */
static bool parse_u32(const char* s, const char* end, uint32_t* out) {
    if(s == end || end - s > 10) return false;
    uint64_t v = 0;
    for(; s < end; s++) {
        if(*s < '0' || *s > '9') return false;
        v = v * 10 + (uint64_t)(*s - '0');
    }
    if(v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly four "XX" tokens separated by spaces. */
static bool parse_hex4(const char* s, uint32_t* out) {
    uint32_t v = 0;
    for(int i = 0; i < 4; i++) {
        if(i > 0) {
            if(!is_ws(*s)) return false;
            while(is_ws(*s)) s++;
        }
        int hi = hex_val(s[0]);
        int lo = hi < 0 ? -1 : hex_val(s[1]);
        if(hi < 0 || lo < 0) return false;
        v |= (uint32_t)((hi << 4) | lo) << (8 * i);
        s += 2;
    }
    if(*s != '\0') return false;
    *out = v;
    return true;
}

/* "0.33", "1", ".5" style; value in (0, 1]; at most 9 fractional digits. */
static bool parse_duty(const char* s, float* out) {
    uint32_t ip = 0, fp = 0, scale = 1;
    int idigits = 0, fdigits = 0;
    for(; *s >= '0' && *s <= '9'; s++) {
        if(++idigits > 1) return false;
        ip = (uint32_t)(*s - '0');
    }
    if(*s == '.') {
        s++;
        for(; *s >= '0' && *s <= '9'; s++) {
            if(++fdigits > 9) return false;
            fp = fp * 10 + (uint32_t)(*s - '0');
            scale *= 10;
        }
    }
    if(*s != '\0' || idigits + fdigits == 0) return false;
    double v = (double)ip + (double)fp / (double)scale;
    if(!(v > 0.0 && v <= 1.0)) return false;
    *out = (float)v;
    return true;
}

static bool name_ok(const char* s, size_t len) {
    if(len == 0 || len >= IR_MAX_NAME) return false;
    for(size_t i = 0; i < len; i++) {
        uint8_t c = (uint8_t)s[i];
        if(c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

static bool protocol_ok(const char* s, size_t len) {
    if(len == 0 || len >= IR_MAX_PROTOCOL) return false;
    for(size_t i = 0; i < len; i++) {
        char c = s[i];
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  c == '_' || c == '-';
        if(!ok) return false;
    }
    return true;
}

/* Validates the whole timings list and counts it. */
static IrParseError parse_timings(const char* s, uint32_t* count) {
    uint32_t n = 0;
    for(;;) {
        while(is_ws(*s)) s++;
        if(*s == '\0') break;
        const char* t = s;
        while(*s && !is_ws(*s)) s++;
        uint32_t v;
        if(!parse_u32(t, s, &v) || v == 0) return IrParseErrBadNumber;
        if(++n > IR_MAX_TIMINGS) return IrParseErrTooManyTimings;
    }
    if(n == 0) return IrParseErrBadNumber;
    *count = n;
    return IrParseOk;
}

static IrParseError finish_signal(IrParseResult* out, const IrSignalInfo* cur, unsigned seen) {
    if(!(seen & SeenName) || !(seen & SeenType)) return IrParseErrMissingField;
    unsigned need, forbid;
    if(cur->type == IrSignalParsed) {
        need = SEEN_PARSED_ONLY;
        forbid = SEEN_RAW_ONLY;
    } else {
        need = SEEN_RAW_ONLY;
        forbid = SEEN_PARSED_ONLY;
    }
    if((seen & need) != need) return IrParseErrMissingField;
    if(seen & forbid) return IrParseErrFieldForType;
    if(out->count >= IR_MAX_SIGNALS) return IrParseErrTooManySignals;
    out->signals[out->count++] = *cur;
    return IrParseOk;
}

static bool key_is(const char* k, size_t klen, const char* lit) {
    return strlen(lit) == klen && memcmp(k, lit, klen) == 0;
}

/* Applies "key: value" to cur. */
static IrParseError apply_field(
    IrSignalInfo* cur,
    unsigned* seen,
    const char* k,
    size_t klen,
    const char* v,
    size_t vlen) {
    unsigned bit;
    if(key_is(k, klen, "name")) bit = SeenName;
    else if(key_is(k, klen, "type")) bit = SeenType;
    else if(key_is(k, klen, "protocol")) bit = SeenProtocol;
    else if(key_is(k, klen, "address")) bit = SeenAddress;
    else if(key_is(k, klen, "command")) bit = SeenCommand;
    else if(key_is(k, klen, "frequency")) bit = SeenFrequency;
    else if(key_is(k, klen, "duty_cycle")) bit = SeenDuty;
    else if(key_is(k, klen, "data")) bit = SeenData;
    else return IrParseErrUnknownKey;

    if(*seen & bit) return IrParseErrDuplicateKey;

    IrParseError e = IrParseOk;
    switch(bit) {
    case SeenName:
        if(!name_ok(v, vlen)) return IrParseErrBadName;
        memcpy(cur->name, v, vlen);
        cur->name[vlen] = '\0';
        break;
    case SeenType:
        if(vlen == 6 && memcmp(v, "parsed", 6) == 0) cur->type = IrSignalParsed;
        else if(vlen == 3 && memcmp(v, "raw", 3) == 0) cur->type = IrSignalRaw;
        else return IrParseErrBadType;
        break;
    case SeenProtocol:
        if(!protocol_ok(v, vlen)) return IrParseErrBadProtocol;
        memcpy(cur->protocol, v, vlen);
        cur->protocol[vlen] = '\0';
        break;
    case SeenAddress:
        if(!parse_hex4(v, &cur->address)) return IrParseErrBadHex;
        break;
    case SeenCommand:
        if(!parse_hex4(v, &cur->command)) return IrParseErrBadHex;
        break;
    case SeenFrequency:
        if(!parse_u32(v, v + vlen, &cur->frequency) || cur->frequency < IR_MIN_FREQUENCY ||
           cur->frequency > IR_MAX_FREQUENCY)
            return IrParseErrBadNumber;
        break;
    case SeenDuty:
        if(!parse_duty(v, &cur->duty_cycle)) return IrParseErrBadNumber;
        break;
    case SeenData:
        e = parse_timings(v, &cur->timings_count);
        break;
    }
    if(e == IrParseOk) *seen |= bit;
    return e;
}

typedef enum {
    StHeaderFiletype,
    StHeaderVersion,
    StBody,
} State;

static IrParseError parse_impl(Reader* r, IrParseResult* out, char* line) {
    State st = StHeaderFiletype;
    IrSignalInfo cur;
    unsigned seen = 0;
    bool in_signal = false;
    size_t lineno = 0;

    for(;;) {
        size_t len = 0;
        LineStatus ls = read_line(r, line, &len);
        if(ls == LineError) return r->err;
        if(ls == LineEof) break;
        lineno++;
        out->error_line = lineno;

        if(len == 0) continue;
        if(memchr(line, '\0', len) != NULL) return IrParseErrMalformedLine;

        if(line[0] == '#') {
            if(in_signal) {
                IrParseError e = finish_signal(out, &cur, seen);
                if(e != IrParseOk) return e;
                in_signal = false;
                seen = 0;
            }
            continue;
        }

        /* split "key: value" */
        char* colon = memchr(line, ':', len);
        if(!colon) return IrParseErrMalformedLine;
        const char* k = line;
        size_t klen = (size_t)(colon - line);
        const char* v = colon + 1;
        const char* vend = line + len;
        while(v < vend && is_ws(*v)) v++;
        while(vend > v && is_ws(vend[-1])) vend--;
        size_t vlen = (size_t)(vend - v);
        line[vend - line] = '\0'; /* v is NUL-terminated at vend */

        if(st == StHeaderFiletype) {
            if(!key_is(k, klen, "Filetype") || vlen != 14 || memcmp(v, "IR signals file", 14) != 0)
                return IrParseErrBadHeader;
            st = StHeaderVersion;
            continue;
        }
        if(st == StHeaderVersion) {
            uint32_t ver;
            if(!key_is(k, klen, "Version") || !parse_u32(v, v + vlen, &ver))
                return IrParseErrBadVersion;
            if(ver != 1) return IrParseErrBadVersion;
            st = StBody;
            continue;
        }

        /* a new "name" starts a new signal even without a '#' separator */
        if(key_is(k, klen, "name") && in_signal) {
            if(seen & SeenName) {
                /* a second name in the same block: if the block is otherwise
                 * complete treat it as a new signal, else duplicate */
                IrParseError e = finish_signal(out, &cur, seen);
                if(e == IrParseErrMissingField) return IrParseErrDuplicateKey;
                if(e != IrParseOk) return e;
                in_signal = false;
                seen = 0;
            }
        }
        if(!in_signal) {
            memset(&cur, 0, sizeof(cur));
            in_signal = true;
            seen = 0;
        }
        IrParseError e = apply_field(&cur, &seen, k, klen, v, vlen);
        if(e != IrParseOk) return e;
    }

    if(r->err != IrParseOk) return r->err;
    if(st != StBody) return st == StHeaderFiletype ? IrParseErrTruncated : IrParseErrTruncated;
    if(in_signal) {
        IrParseError e = finish_signal(out, &cur, seen);
        if(e != IrParseOk) return e;
    }
    return IrParseOk;
}

IrParseError ir_parse(IrReadFn read, void* ctx, IrParseResult* out) {
    if(!out) return IrParseErrRead;
    out->count = 0;
    out->error = IrParseOk;
    out->error_line = 0;
    if(!read) {
        out->error = IrParseErrRead;
        return out->error;
    }
    Reader r;
    memset(&r, 0, sizeof(r));
    r.read = read;
    r.ctx = ctx;
    IrParseError e = parse_impl(&r, out, out->line_scratch);
    if(e != IrParseOk) out->count = 0;
    else out->error_line = 0;
    out->error = e;
    return e;
}

typedef struct {
    const uint8_t* data;
    size_t len, pos;
} MemSrc;

static int32_t mem_read(void* ctx, uint8_t* buf, size_t max_len) {
    MemSrc* m = ctx;
    size_t n = m->len - m->pos;
    if(n > max_len) n = max_len;
    if(n > 0) memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return (int32_t)n;
}

IrParseError ir_parse_buffer(const uint8_t* data, size_t len, IrParseResult* out) {
    MemSrc m = {data, data ? len : 0, 0};
    return ir_parse(mem_read, &m, out);
}

const char* ir_parse_error_str(IrParseError e) {
    switch(e) {
    case IrParseOk: return "ok";
    case IrParseErrRead: return "read error";
    case IrParseErrFileTooLarge: return "file too large";
    case IrParseErrLineTooLong: return "line too long";
    case IrParseErrBadHeader: return "bad Filetype header";
    case IrParseErrBadVersion: return "unsupported or bad Version";
    case IrParseErrTruncated: return "truncated file";
    case IrParseErrUnknownKey: return "unsupported key";
    case IrParseErrDuplicateKey: return "duplicate key";
    case IrParseErrMalformedLine: return "malformed line";
    case IrParseErrBadName: return "bad name";
    case IrParseErrBadType: return "bad type";
    case IrParseErrBadProtocol: return "bad protocol";
    case IrParseErrBadHex: return "bad address/command";
    case IrParseErrBadNumber: return "bad number";
    case IrParseErrTooManyTimings: return "too many timings";
    case IrParseErrTooManySignals: return "too many signals";
    case IrParseErrMissingField: return "missing field";
    case IrParseErrFieldForType: return "field not valid for type";
    }
    return "unknown";
}
