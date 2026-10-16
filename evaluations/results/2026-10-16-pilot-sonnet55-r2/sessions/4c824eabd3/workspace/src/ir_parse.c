#include "ir_parse.h"
#include <string.h>

enum {
    KName = 1,
    KType = 2,
    KProto = 4,
    KAddr = 8,
    KCmd = 16,
    KFreq = 32,
    KDuty = 64,
    KData = 128,
};

static bool is_print(unsigned char c) {
    return c >= 0x20 && c < 0x7f;
}

static bool eq(const char* s, size_t n, const char* lit) {
    return strlen(lit) == n && memcmp(s, lit, n) == 0;
}

static int hexv(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Strict unsigned decimal, digits only, must fit uint32.
static bool parse_u32(const char* s, size_t n, uint32_t* v) {
    if(n == 0 || n > 10) return false;
    uint64_t acc = 0;
    for(size_t i = 0; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        acc = acc * 10 + (uint64_t)(s[i] - '0');
    }
    if(acc > UINT32_MAX) return false;
    *v = (uint32_t)acc;
    return true;
}

// Exactly 4 space-separated hex bytes ("04 00 00 00").
static bool parse_hex4(const char* s, size_t n, uint32_t* v) {
    if(n != 11) return false;
    uint32_t acc = 0;
    for(int b = 0; b < 4; b++) {
        size_t p = (size_t)b * 3;
        int hi = hexv(s[p]);
        int lo = hexv(s[p + 1]);
        if(hi < 0 || lo < 0) return false;
        if(b < 3 && s[p + 2] != ' ') return false;
        acc |= (uint32_t)((hi << 4) | lo) << (8 * b);
    }
    *v = acc;
    return true;
}

// Plain decimal in (0, 1], e.g. "0.330000" or "1". No sign, exponent, nan or inf.
static bool parse_duty(const char* s, size_t n, float* v) {
    if(n == 0 || n > 16) return false;
    size_t i = 0;
    size_t digits = 0;
    double whole = 0;
    double frac = 0;
    double scale = 1;
    for(; i < n && s[i] >= '0' && s[i] <= '9'; i++, digits++) {
        whole = whole * 10 + (s[i] - '0');
        if(whole > 1) return false;
    }
    if(i < n && s[i] == '.') {
        for(i++; i < n && s[i] >= '0' && s[i] <= '9'; i++, digits++) {
            scale /= 10;
            frac += (s[i] - '0') * scale;
        }
    }
    if(i != n || digits == 0) return false;
    double r = whole + frac;
    if(r > 1.0 || r <= 0.0) return false;
    *v = (float)r;
    return true;
}

// Counts and validates raw timings: positive uint32 decimals, single-space separated.
static IrError count_timings(const char* s, size_t n, uint32_t* count) {
    uint32_t c = 0;
    size_t i = 0;
    while(i < n) {
        size_t st = i;
        while(i < n && s[i] != ' ') i++;
        uint32_t t;
        if(!parse_u32(s + st, i - st, &t) || t == 0) return IrErrBadValue;
        if(c >= IR_MAX_TIMINGS) return IrErrTooManyTimings;
        c++;
        if(i < n) {
            i++;
            if(i == n) return IrErrBadValue; // trailing space
        }
    }
    if(c == 0) return IrErrBadValue;
    *count = c;
    return IrOk;
}

static bool signal_complete(const IrSignal* s, unsigned seen) {
    if(!(seen & KName) || !(seen & KType)) return false;
    unsigned need = s->type == IrSignalParsed ? (KProto | KAddr | KCmd) :
                                                (KFreq | KDuty | KData);
    return (seen & need) == need;
}

const char* ir_error_str(IrError e) {
    switch(e) {
    case IrOk: return "ok";
    case IrErrTooLarge: return "file too large";
    case IrErrEmpty: return "empty file";
    case IrErrBadHeader: return "not an IR signals file";
    case IrErrBadVersion: return "unsupported version";
    case IrErrLineTooLong: return "line too long";
    case IrErrBadLine: return "malformed line";
    case IrErrUnknownKey: return "unsupported key";
    case IrErrDuplicateKey: return "duplicate key";
    case IrErrBadValue: return "invalid value";
    case IrErrTooManySignals: return "too many signals";
    case IrErrTooManyTimings: return "too many timings";
    case IrErrIncomplete: return "incomplete signal";
    }
    return "unknown error";
}

#define FAIL(e) \
    do { \
        out->error = (e); \
        out->error_line = lineno; \
        return (e); \
    } while(0)

static IrError push_signal(IrFile* out, const IrSignal* s, unsigned seen, size_t lineno) {
    if(!signal_complete(s, seen)) FAIL(IrErrIncomplete);
    if(out->count >= IR_MAX_SIGNALS) FAIL(IrErrTooManySignals);
    out->signals[out->count++] = *s;
    return IrOk;
}

IrError ir_parse(const char* data, size_t len, IrFile* out) {
    size_t lineno = 0;
    memset(out, 0, sizeof(*out));
    if(len == 0) FAIL(IrErrEmpty);
    if(!data) FAIL(IrErrBadLine);
    if(len > IR_MAX_FILE_SIZE) FAIL(IrErrTooLarge);

    size_t pos = 0;
    unsigned seen = 0;
    bool in_signal = false;
    bool have_ft = false;
    bool have_ver = false;
    IrSignal cur;
    memset(&cur, 0, sizeof(cur));

    while(pos < len) {
        size_t start = pos;
        while(pos < len && data[pos] != '\n') pos++;
        size_t end = pos;
        if(pos < len) pos++;
        lineno++;
        if(end > start && data[end - 1] == '\r') end--;
        size_t ll = end - start;
        if(ll > IR_MAX_LINE_LEN) FAIL(IrErrLineTooLong);
        const char* line = data + start;
        if(ll && memchr(line, '\0', ll)) FAIL(IrErrBadLine);

        if(ll == 0) continue;
        if(line[0] == '#') {
            if(in_signal) {
                IrError e = push_signal(out, &cur, seen, lineno);
                if(e != IrOk) return e;
                in_signal = false;
                seen = 0;
            }
            continue;
        }

        const char* colon = memchr(line, ':', ll);
        if(!colon) FAIL(IrErrBadLine);
        size_t kl = (size_t)(colon - line);
        const char* v = colon + 1;
        size_t vl = ll - kl - 1;
        if(vl > 0) {
            if(*v != ' ') FAIL(IrErrBadLine);
            v++;
            vl--;
        }

        if(!have_ft) {
            if(!eq(line, kl, "Filetype") || !eq(v, vl, "IR signals file")) FAIL(IrErrBadHeader);
            have_ft = true;
            continue;
        }
        if(!have_ver) {
            uint32_t ver;
            if(!eq(line, kl, "Version")) FAIL(IrErrBadHeader);
            if(!parse_u32(v, vl, &ver) || ver != 1) FAIL(IrErrBadVersion);
            have_ver = true;
            continue;
        }

        unsigned key;
        if(eq(line, kl, "name")) key = KName;
        else if(eq(line, kl, "type")) key = KType;
        else if(eq(line, kl, "protocol")) key = KProto;
        else if(eq(line, kl, "address")) key = KAddr;
        else if(eq(line, kl, "command")) key = KCmd;
        else if(eq(line, kl, "frequency")) key = KFreq;
        else if(eq(line, kl, "duty_cycle")) key = KDuty;
        else if(eq(line, kl, "data")) key = KData;
        else FAIL(IrErrUnknownKey);

        if(!in_signal) {
            memset(&cur, 0, sizeof(cur));
            in_signal = true;
            seen = 0;
        }
        if(seen & key) FAIL(IrErrDuplicateKey);
        seen |= key;

        switch(key) {
        case KName:
            if(vl == 0 || vl > IR_MAX_NAME_LEN) FAIL(IrErrBadValue);
            for(size_t i = 0; i < vl; i++)
                if(!is_print((unsigned char)v[i])) FAIL(IrErrBadValue);
            memcpy(cur.name, v, vl);
            break;
        case KType:
            if(eq(v, vl, "parsed")) cur.type = IrSignalParsed;
            else if(eq(v, vl, "raw")) cur.type = IrSignalRaw;
            else FAIL(IrErrBadValue);
            break;
        case KProto:
            if(vl == 0 || vl > IR_MAX_PROTO_LEN) FAIL(IrErrBadValue);
            for(size_t i = 0; i < vl; i++)
                if(!is_print((unsigned char)v[i]) || v[i] == ' ') FAIL(IrErrBadValue);
            memcpy(cur.protocol, v, vl);
            break;
        case KAddr:
            if(!parse_hex4(v, vl, &cur.address)) FAIL(IrErrBadValue);
            break;
        case KCmd:
            if(!parse_hex4(v, vl, &cur.command)) FAIL(IrErrBadValue);
            break;
        case KFreq:
            // Flipper accepts 10 kHz..56 kHz carriers.
            if(!parse_u32(v, vl, &cur.frequency) || cur.frequency < 10000 ||
               cur.frequency > 56000)
                FAIL(IrErrBadValue);
            break;
        case KDuty:
            if(!parse_duty(v, vl, &cur.duty_cycle)) FAIL(IrErrBadValue);
            break;
        case KData: {
            IrError e = count_timings(v, vl, &cur.timings_count);
            if(e != IrOk) FAIL(e);
            break;
        }
        }
    }

    if(!have_ft || !have_ver) FAIL(IrErrBadHeader);
    if(in_signal) {
        IrError e = push_signal(out, &cur, seen, lineno);
        if(e != IrOk) return e;
    }
    return IrOk;
}
