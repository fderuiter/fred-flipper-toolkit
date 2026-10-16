// Reviewer-side reference solution for FRED task 04. MIT licensed, original.
#include "ir_parser.h"
#include <string.h>

enum {
    F_NAME = 1u << 0, F_TYPE = 1u << 1, F_PROTOCOL = 1u << 2, F_ADDRESS = 1u << 3,
    F_COMMAND = 1u << 4, F_FREQUENCY = 1u << 5, F_DUTY = 1u << 6, F_DATA = 1u << 7,
};

static const char* const error_names[] = {
    "ok", "too_large", "header", "version", "line_too_long", "syntax", "unknown_key",
    "duplicate_key", "name", "type", "hex", "number", "range", "missing_field",
    "too_many_signals", "too_many_timings",
};

const char* ir_error_name(IrError code) {
    return (unsigned)code < sizeof(error_names) / sizeof(error_names[0]) ? error_names[code] : "?";
}

static bool parse_u32(const char* s, size_t n, uint32_t* out) {
    if(n == 0 || n > 10) return false;
    uint64_t v = 0;
    for(size_t i = 0; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') return false;
        v = v * 10u + (uint64_t)(s[i] - '0');
    }
    if(v > UINT32_MAX) return false;
    *out = (uint32_t)v;
    return true;
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Exactly four space-separated two-digit hex bytes. */
static bool parse_hex4(const char* s, size_t n, uint8_t out[4]) {
    if(n != 11) return false;
    for(size_t i = 0; i < 4; i++) {
        int hi = hex_nibble(s[i * 3]), lo = hex_nibble(s[i * 3 + 1]);
        if(hi < 0 || lo < 0) return false;
        if(i < 3 && s[i * 3 + 2] != ' ') return false;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return true;
}

/* "0.33", "0.330000", "1" -> permille in [0, 1000]. */
static bool parse_duty(const char* s, size_t n, uint32_t* permille) {
    size_t i = 0;
    uint32_t whole = 0, frac = 0, scale = 1000;
    if(n == 0 || n > 12) return false;
    if(s[i] < '0' || s[i] > '9') return false;
    while(i < n && s[i] >= '0' && s[i] <= '9') {
        whole = whole * 10u + (uint32_t)(s[i] - '0');
        if(whole > 1) return false;
        i++;
    }
    if(i < n) {
        if(s[i++] != '.' || i == n) return false;
        for(; i < n; i++) {
            if(s[i] < '0' || s[i] > '9') return false;
            if(scale > 1) {
                scale /= 10;
                frac += (uint32_t)(s[i] - '0') * scale;
            }
        }
    }
    uint32_t v = whole * 1000u + frac;
    if(v > 1000u) return false;
    *permille = v;
    return true;
}

static IrError finish_signal(const IrSignal* sig, uint32_t fields) {
    if(!(fields & F_NAME) || !(fields & F_TYPE)) return IrErrMissingField;
    if(sig->type == IrSignalParsed) {
        if((fields & (F_PROTOCOL | F_ADDRESS | F_COMMAND)) != (F_PROTOCOL | F_ADDRESS | F_COMMAND))
            return IrErrMissingField;
        if(fields & (F_FREQUENCY | F_DUTY | F_DATA)) return IrErrSyntax;
    } else {
        if((fields & (F_FREQUENCY | F_DUTY | F_DATA)) != (F_FREQUENCY | F_DUTY | F_DATA))
            return IrErrMissingField;
        if(fields & (F_PROTOCOL | F_ADDRESS | F_COMMAND)) return IrErrSyntax;
    }
    return IrOk;
}

IrResult ir_parse(const char* data, size_t len, IrRemote* out) {
    IrResult res = {IrOk, 0};
    if(!data || !out) return (IrResult){IrErrSyntax, 0};
    if(len > IR_MAX_FILE_BYTES) return (IrResult){IrErrTooLarge, 0};
    memset(out, 0, sizeof(*out));

    size_t pos = 0;
    uint32_t line_no = 0;
    IrSignal* sig = NULL;
    uint32_t fields = 0;

    while(pos < len) {
        size_t start = pos;
        while(pos < len && data[pos] != '\n') pos++;
        size_t n = pos - start;
        if(pos < len) pos++; /* consume '\n' */
        line_no++;
        const char* line = data + start;
        if(n > 0 && line[n - 1] == '\r') n--;
        if(n > IR_MAX_DATA_LINE) return (IrResult){IrErrLineTooLong, line_no};
        if(n > IR_MAX_LINE && !(n > 6 && memcmp(line, "data: ", 6) == 0))
            return (IrResult){IrErrLineTooLong, line_no};

        if(line_no == 1) {
            static const char hdr[] = "Filetype: IR signals file";
            if(n != sizeof(hdr) - 1 || memcmp(line, hdr, n) != 0) return (IrResult){IrErrHeader, 1};
            continue;
        }
        if(line_no == 2) {
            static const char ver[] = "Version: ";
            uint32_t v;
            if(n < sizeof(ver) - 1 || memcmp(line, ver, sizeof(ver) - 1) != 0)
                return (IrResult){IrErrHeader, 2};
            if(!parse_u32(line + sizeof(ver) - 1, n - (sizeof(ver) - 1), &v) || v != 1)
                return (IrResult){IrErrVersion, 2};
            continue;
        }
        if(n == 0 || line[0] == '#') continue; /* separator or comment */

        const char* sep = NULL;
        for(size_t i = 0; i + 1 < n; i++) {
            if(line[i] == ':' && line[i + 1] == ' ') {
                sep = line + i;
                break;
            }
        }
        if(!sep) return (IrResult){IrErrSyntax, line_no};
        size_t klen = (size_t)(sep - line);
        const char* val = sep + 2;
        size_t vlen = n - klen - 2;

#define KEY_IS(k) (klen == sizeof(k) - 1 && memcmp(line, k, klen) == 0)
        if(KEY_IS("name")) {
            if(sig) {
                /* A second name before any other key is a duplicate, not a new signal. */
                if(fields == F_NAME) return (IrResult){IrErrDuplicateKey, line_no};
                IrError e = finish_signal(sig, fields);
                if(e != IrOk) return (IrResult){e, line_no};
            }
            if(out->signal_count >= IR_MAX_SIGNALS) return (IrResult){IrErrTooManySignals, line_no};
            if(vlen == 0 || vlen > IR_NAME_MAX) return (IrResult){IrErrName, line_no};
            for(size_t i = 0; i < vlen; i++) {
                if((unsigned char)val[i] < 0x20 || (unsigned char)val[i] > 0x7E)
                    return (IrResult){IrErrName, line_no};
            }
            sig = &out->signals[out->signal_count++];
            memcpy(sig->name, val, vlen);
            sig->name[vlen] = '\0';
            fields = F_NAME;
            continue;
        }
        if(!sig) return (IrResult){IrErrSyntax, line_no};

        uint32_t flag;
        if(KEY_IS("type")) flag = F_TYPE;
        else if(KEY_IS("protocol")) flag = F_PROTOCOL;
        else if(KEY_IS("address")) flag = F_ADDRESS;
        else if(KEY_IS("command")) flag = F_COMMAND;
        else if(KEY_IS("frequency")) flag = F_FREQUENCY;
        else if(KEY_IS("duty_cycle")) flag = F_DUTY;
        else if(KEY_IS("data")) flag = F_DATA;
        else return (IrResult){IrErrUnknownKey, line_no};
#undef KEY_IS
        if(fields & flag) return (IrResult){IrErrDuplicateKey, line_no};
        fields |= flag;

        switch(flag) {
        case F_TYPE:
            if(vlen == 6 && memcmp(val, "parsed", 6) == 0) sig->type = IrSignalParsed;
            else if(vlen == 3 && memcmp(val, "raw", 3) == 0) sig->type = IrSignalRaw;
            else return (IrResult){IrErrType, line_no};
            break;
        case F_PROTOCOL:
            if(vlen == 0 || vlen > IR_PROTOCOL_MAX) return (IrResult){IrErrRange, line_no};
            for(size_t i = 0; i < vlen; i++) {
                char c = val[i];
                if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
                    return (IrResult){IrErrSyntax, line_no};
            }
            memcpy(sig->protocol, val, vlen);
            sig->protocol[vlen] = '\0';
            break;
        case F_ADDRESS:
            if(!parse_hex4(val, vlen, sig->address)) return (IrResult){IrErrHex, line_no};
            break;
        case F_COMMAND:
            if(!parse_hex4(val, vlen, sig->command)) return (IrResult){IrErrHex, line_no};
            break;
        case F_FREQUENCY:
            if(!parse_u32(val, vlen, &sig->frequency)) return (IrResult){IrErrNumber, line_no};
            if(sig->frequency == 0) return (IrResult){IrErrRange, line_no};
            break;
        case F_DUTY:
            if(!parse_duty(val, vlen, &sig->duty_permille)) return (IrResult){IrErrNumber, line_no};
            break;
        case F_DATA: {
            sig->timing_offset = out->timing_used;
            size_t i = 0;
            while(i < vlen) {
                size_t t = i;
                while(i < vlen && val[i] != ' ') i++;
                uint32_t v;
                if(!parse_u32(val + t, i - t, &v) || v == 0) return (IrResult){IrErrNumber, line_no};
                if(sig->timing_count >= IR_MAX_TIMINGS) return (IrResult){IrErrTooManyTimings, line_no};
                if(out->timing_used >= IR_MAX_TIMING_POOL) return (IrResult){IrErrTooManyTimings, line_no};
                out->timings[out->timing_used++] = v;
                sig->timing_count++;
                if(i < vlen) i++; /* single space separator */
                if(i == vlen && val[vlen - 1] == ' ') return (IrResult){IrErrSyntax, line_no};
            }
            if(sig->timing_count == 0) return (IrResult){IrErrNumber, line_no};
            break;
        }
        default:
            break;
        }
    }
    if(line_no < 2) return (IrResult){line_no == 0 ? IrErrHeader : IrErrVersion, line_no};
    if(sig) {
        IrError e = finish_signal(sig, fields);
        if(e != IrOk) return (IrResult){e, line_no};
    }
    return res;
}
