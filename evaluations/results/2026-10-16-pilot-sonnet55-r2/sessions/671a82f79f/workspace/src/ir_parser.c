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

static IrError fail(IrParser* p, IrError e) {
    if(p->error == IrOk) {
        p->error = e;
        p->error_line = p->line_no;
    }
    return p->error;
}

static bool parse_u32(const char* s, size_t len, uint32_t* v) {
    if(len == 0 || len > 10) return false;
    uint64_t acc = 0;
    for(size_t i = 0; i < len; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        acc = acc * 10u + (uint64_t)(s[i] - '0');
    }
    if(acc > UINT32_MAX) return false;
    *v = (uint32_t)acc;
    return true;
}

static int hex_val(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly "XX XX XX XX". */
static bool parse_hex4(const char* s, uint8_t out[4]) {
    if(strlen(s) != 11) return false;
    for(size_t i = 0; i < 4; i++) {
        int hi = hex_val(s[i * 3]);
        int lo = hex_val(s[i * 3 + 1]);
        if(hi < 0 || lo < 0) return false;
        if(i < 3 && s[i * 3 + 2] != ' ') return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* "0", "1", "0.33", "1.0" ... 1-6 fraction digits; result in (0, 1e6] ppm. */
static bool parse_duty(const char* s, uint32_t* ppm) {
    if(s[0] != '0' && s[0] != '1') return false;
    uint32_t v = (uint32_t)(s[0] - '0') * 1000000u;
    if(s[1] != '\0') {
        if(s[1] != '.') return false;
        const char* f = s + 2;
        size_t n = strlen(f);
        if(n < 1 || n > 6) return false;
        uint32_t scale = 100000u;
        for(size_t i = 0; i < n; i++) {
            if(f[i] < '0' || f[i] > '9') return false;
            v += (uint32_t)(f[i] - '0') * scale;
            scale /= 10u;
        }
    }
    if(v == 0 || v > 1000000u) return false;
    *ppm = v;
    return true;
}

/* Validates every token as uint32 and counts them, storing nothing. */
static IrError parse_data(const char* s, uint32_t* count) {
    uint32_t n = 0;
    while(*s) {
        while(*s == ' ') s++;
        if(!*s) break;
        const char* t = s;
        while(*s && *s != ' ') s++;
        uint32_t v;
        if(!parse_u32(t, (size_t)(s - t), &v) || v == 0) return IrErrBadValue;
        if(n >= IR_MAX_TIMINGS) return IrErrTooManyTimings;
        n++;
    }
    if(n == 0) return IrErrBadValue;
    *count = n;
    return IrOk;
}

static bool valid_protocol(const char* s) {
    size_t n = strlen(s);
    if(n == 0 || n > IR_MAX_PROTOCOL) return false;
    for(size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if(!ok) return false;
    }
    return true;
}

static void begin_signal(IrParser* p) {
    memset(&p->cur, 0, sizeof(p->cur));
    p->seen = 0;
    p->in_signal = true;
}

static IrError finalize_signal(IrParser* p) {
    if(!p->in_signal) return IrOk;
    p->in_signal = false;
    uint32_t s = p->seen;

    if(!(s & KeyName) || !(s & KeyType)) return fail(p, IrErrMissingKey);
    uint32_t need = (p->cur.type == IrSignalParsed) ? PARSED_KEYS : RAW_KEYS;
    uint32_t forbid = (p->cur.type == IrSignalParsed) ? RAW_KEYS : PARSED_KEYS;
    if(s & forbid) return fail(p, IrErrWrongFields);
    if((s & need) != need) return fail(p, IrErrMissingKey);
    if(p->out->count >= IR_MAX_SIGNALS) return fail(p, IrErrTooManySignals);

    p->out->signals[p->out->count++] = p->cur;
    return IrOk;
}

static IrError handle_key(IrParser* p, const char* key, const char* val) {
    uint32_t bit;
    if(strcmp(key, "name") == 0) bit = KeyName;
    else if(strcmp(key, "type") == 0) bit = KeyType;
    else if(strcmp(key, "protocol") == 0) bit = KeyProtocol;
    else if(strcmp(key, "address") == 0) bit = KeyAddress;
    else if(strcmp(key, "command") == 0) bit = KeyCommand;
    else if(strcmp(key, "frequency") == 0) bit = KeyFrequency;
    else if(strcmp(key, "duty_cycle") == 0) bit = KeyDuty;
    else if(strcmp(key, "data") == 0) bit = KeyData;
    else return fail(p, IrErrUnknownKey);

    if(!p->in_signal) begin_signal(p);
    if(p->seen & bit) return fail(p, IrErrDuplicateKey);
    p->seen |= bit;

    IrSignal* c = &p->cur;
    size_t vlen = strlen(val);
    switch(bit) {
    case KeyName:
        if(vlen == 0 || vlen > IR_MAX_NAME) return fail(p, IrErrBadValue);
        memcpy(c->name, val, vlen + 1);
        break;
    case KeyType:
        if(strcmp(val, "parsed") == 0) c->type = IrSignalParsed;
        else if(strcmp(val, "raw") == 0) c->type = IrSignalRaw;
        else return fail(p, IrErrBadValue);
        break;
    case KeyProtocol:
        if(!valid_protocol(val)) return fail(p, IrErrBadValue);
        memcpy(c->protocol, val, vlen + 1);
        break;
    case KeyAddress:
        if(!parse_hex4(val, c->address)) return fail(p, IrErrBadValue);
        break;
    case KeyCommand:
        if(!parse_hex4(val, c->command)) return fail(p, IrErrBadValue);
        break;
    case KeyFrequency:
        if(!parse_u32(val, vlen, &c->frequency) || c->frequency < IR_MIN_FREQUENCY ||
           c->frequency > IR_MAX_FREQUENCY)
            return fail(p, IrErrBadValue);
        break;
    case KeyDuty:
        if(!parse_duty(val, &c->duty_cycle_ppm)) return fail(p, IrErrBadValue);
        break;
    case KeyData: {
        IrError e = parse_data(val, &c->timing_count);
        if(e != IrOk) return fail(p, e);
        break;
    }
    default:
        break;
    }
    return IrOk;
}

static IrError handle_line(IrParser* p) {
    p->line_no++;
    size_t len = p->line_len;
    p->line_len = 0;
    if(len > 0 && p->line[len - 1] == '\r') len--;
    p->line[len] = '\0';

    for(size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)p->line[i];
        if(c < 0x20 || c > 0x7e) return fail(p, IrErrBadChar);
    }

    if(p->line_no == 1) {
        return strcmp(p->line, "Filetype: IR signals file") == 0 ? IrOk :
                                                                    fail(p, IrErrHeader);
    }
    if(p->line_no == 2) {
        return strcmp(p->line, "Version: 1") == 0 ? IrOk : fail(p, IrErrVersion);
    }

    if(len == 0) return IrOk;
    if(p->line[0] == '#') return finalize_signal(p);

    char* colon = strchr(p->line, ':');
    if(!colon) return fail(p, IrErrSyntax);
    *colon = '\0';
    char* val = colon + 1;
    if(*val == ' ') val++;
    else if(*val != '\0') return fail(p, IrErrSyntax);
    return handle_key(p, p->line, val);
}

void ir_parser_init(IrParser* p, IrFile* out) {
    memset(p, 0, sizeof(*p));
    memset(out, 0, sizeof(*out));
    p->out = out;
}

IrError ir_parser_feed(IrParser* p, const uint8_t* data, size_t len) {
    for(size_t i = 0; i < len && p->error == IrOk; i++) {
        if(++p->total_bytes > IR_MAX_FILE_BYTES) {
            fail(p, IrErrFileTooBig);
            break;
        }
        if(data[i] == '\n') {
            handle_line(p);
        } else if(p->line_len >= IR_MAX_LINE) {
            p->line_no++; /* report the line being read */
            fail(p, IrErrLineTooLong);
            p->line_no--;
        } else {
            p->line[p->line_len++] = (char)data[i];
        }
    }
    return p->error;
}

IrError ir_parser_finish(IrParser* p) {
    if(p->error != IrOk) return p->error;
    if(p->line_len > 0) {
        if(handle_line(p) != IrOk) return p->error;
    } else if(p->line_no < 2) {
        p->line_no++;
        fail(p, IrErrTruncated);
        return p->error;
    }
    if(p->line_no < 2) {
        fail(p, IrErrTruncated);
        return p->error;
    }
    finalize_signal(p);
    return p->error;
}

IrError ir_parse_buffer(IrParser* p, IrFile* out, const uint8_t* data, size_t len) {
    ir_parser_init(p, out);
    if(ir_parser_feed(p, data, len) != IrOk) return p->error;
    return ir_parser_finish(p);
}

const char* ir_error_string(IrError e) {
    switch(e) {
    case IrOk: return "ok";
    case IrErrHeader: return "not an IR signals file";
    case IrErrVersion: return "unsupported version";
    case IrErrTruncated: return "file truncated";
    case IrErrBadChar: return "invalid character";
    case IrErrLineTooLong: return "line too long";
    case IrErrFileTooBig: return "file too big";
    case IrErrSyntax: return "syntax error";
    case IrErrUnknownKey: return "unsupported key";
    case IrErrDuplicateKey: return "duplicate key";
    case IrErrBadValue: return "bad value";
    case IrErrMissingKey: return "missing key";
    case IrErrWrongFields: return "field not valid for signal type";
    case IrErrTooManySignals: return "too many signals";
    case IrErrTooManyTimings: return "too many timings";
    }
    return "unknown error";
}
