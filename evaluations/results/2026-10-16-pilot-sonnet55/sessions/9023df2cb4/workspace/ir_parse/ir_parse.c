#include "ir_parse.h"

#include <string.h>

enum {
    KeyFiletype,
    KeyVersion,
    KeyName,
    KeyType,
    KeyProtocol,
    KeyAddress,
    KeyCommand,
    KeyFrequency,
    KeyDuty,
    KeyData,
    KeyNone,
};

#define BIT(k) ((uint8_t)(1u << (k)))
/* seen-mask bits use ids shifted down by 2 so all body keys fit in a uint8_t */
#define SB(k) BIT((k) - KeyName)

#define MASK_PARSED (SB(KeyName) | SB(KeyType) | SB(KeyProtocol) | SB(KeyAddress) | SB(KeyCommand))
#define MASK_RAW (SB(KeyName) | SB(KeyType) | SB(KeyFrequency) | SB(KeyDuty) | SB(KeyData))

enum {
    StLineStart,
    StComment,
    StKey,
    StSpace,
    StValue,
};

static const struct {
    const char* text;
    uint8_t id;
} KEYS[] = {
    {"Filetype", KeyFiletype},
    {"Version", KeyVersion},
    {"name", KeyName},
    {"type", KeyType},
    {"protocol", KeyProtocol},
    {"address", KeyAddress},
    {"command", KeyCommand},
    {"frequency", KeyFrequency},
    {"duty_cycle", KeyDuty},
    {"data", KeyData},
};

static void fail(IrParser* p, IrStatus s) {
    if(p->status == IrOk) {
        p->status = s;
        p->error_line = p->line_no;
    }
}

void ir_parser_init(IrParser* p) {
    memset(p, 0, sizeof(*p));
    p->line_no = 1;
    p->state = StLineStart;
    p->key_id = KeyNone;
}

/* ---------- value parsers ---------- */

/* Decimal digits only, 1..10 chars, must fit uint32. */
static bool parse_u32(IrParser* p, const char* s, uint32_t* out) {
    size_t n = strlen(s);
    if(n == 0 || n > 10) {
        fail(p, n == 0 ? IrErrBadValue : IrErrOverflow);
        return false;
    }
    uint64_t v = 0;
    for(size_t i = 0; i < n; i++) {
        if(s[i] < '0' || s[i] > '9') {
            fail(p, IrErrBadValue);
            return false;
        }
        v = v * 10 + (uint64_t)(s[i] - '0');
    }
    if(v > UINT32_MAX) {
        fail(p, IrErrOverflow);
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static int hex_digit(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Exactly "XX XX XX XX". */
static bool parse_hex4(IrParser* p, const char* s, uint8_t out[4]) {
    if(strlen(s) != 11) {
        fail(p, IrErrBadValue);
        return false;
    }
    for(int i = 0; i < 4; i++) {
        int hi = hex_digit(s[i * 3]);
        int lo = hex_digit(s[i * 3 + 1]);
        if(hi < 0 || lo < 0 || (i < 3 && s[i * 3 + 2] != ' ')) {
            fail(p, IrErrBadValue);
            return false;
        }
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return true;
}

/* "0", "1", "0.33", "0.330000" ... -> parts per million, must be 1..1000000. */
static bool parse_duty(IrParser* p, const char* s, uint32_t* out) {
    if((s[0] != '0' && s[0] != '1')) {
        fail(p, IrErrBadValue);
        return false;
    }
    uint32_t ppm = (uint32_t)(s[0] - '0') * 1000000u;
    size_t n = strlen(s);
    if(n > 1) {
        if(s[1] != '.' || n == 2 || n > 11) {
            fail(p, IrErrBadValue);
            return false;
        }
        uint32_t scale = 100000u;
        for(size_t i = 2; i < n; i++) {
            if(s[i] < '0' || s[i] > '9') {
                fail(p, IrErrBadValue);
                return false;
            }
            ppm += (uint32_t)(s[i] - '0') * scale; /* digits past 6 add 0 */
            scale /= 10;
        }
    }
    if(ppm == 0 || ppm > 1000000u) {
        fail(p, IrErrRange);
        return false;
    }
    *out = ppm;
    return true;
}

static bool valid_protocol(const char* s) {
    size_t n = strlen(s);
    if(n == 0 || n > IR_PROTOCOL_MAX) return false;
    for(size_t i = 0; i < n; i++) {
        char c = s[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if(!ok) return false;
    }
    return true;
}

/* ---------- signal assembly ---------- */

static void commit_signal(IrParser* p) {
    if(!p->open) return;
    p->open = false;
    bool ok = (p->seen & SB(KeyType)) &&
              ((p->cur.type == IrTypeParsed && p->seen == MASK_PARSED) ||
               (p->cur.type == IrTypeRaw && p->seen == MASK_RAW));
    if(!ok) {
        fail(p, IrErrIncomplete);
        return;
    }
    if(p->count >= IR_MAX_BUTTONS) {
        fail(p, IrErrTooManyButtons);
        return;
    }
    p->buttons[p->count++] = p->cur;
}

static void finish_token(IrParser* p) {
    if(p->tok_len == 0) return;
    p->tok[p->tok_len] = '\0';
    p->tok_len = 0;
    uint32_t v;
    if(!parse_u32(p, p->tok, &v)) return;
    if(v == 0) {
        fail(p, IrErrBadValue);
        return;
    }
    if(p->cur.timing_count >= IR_MAX_TIMINGS) {
        fail(p, IrErrTooManyTimings);
        return;
    }
    p->cur.timing_count++;
}

static void end_value(IrParser* p) {
    if(p->key_id == KeyData) {
        finish_token(p);
        if(p->status == IrOk && p->cur.timing_count == 0) fail(p, IrErrBadValue);
        return;
    }
    while(p->val_len > 0 && p->val[p->val_len - 1] == ' ') p->val_len--;
    p->val[p->val_len] = '\0';
    const char* v = p->val;

    switch(p->key_id) {
    case KeyFiletype:
        if(strcmp(v, "IR signals file") != 0) fail(p, IrErrFiletype);
        else p->stage = 1;
        break;
    case KeyVersion:
        if(strcmp(v, "1") != 0) fail(p, IrErrVersion);
        else p->stage = 2;
        break;
    case KeyName:
        commit_signal(p);
        if(p->status != IrOk) break;
        if(p->val_len == 0 || p->val_len > IR_NAME_MAX) {
            fail(p, IrErrBadName);
            break;
        }
        memset(&p->cur, 0, sizeof(p->cur));
        memcpy(p->cur.name, v, p->val_len + 1u);
        p->open = true;
        p->seen = SB(KeyName);
        break;
    case KeyType:
        if(strcmp(v, "parsed") == 0) p->cur.type = IrTypeParsed;
        else if(strcmp(v, "raw") == 0) p->cur.type = IrTypeRaw;
        else fail(p, IrErrBadValue);
        break;
    case KeyProtocol:
        if(!valid_protocol(v)) fail(p, IrErrBadValue);
        else memcpy(p->cur.protocol, v, strlen(v) + 1u);
        break;
    case KeyAddress:
        parse_hex4(p, v, p->cur.address);
        break;
    case KeyCommand:
        parse_hex4(p, v, p->cur.command);
        break;
    case KeyFrequency: {
        uint32_t f;
        if(parse_u32(p, v, &f)) {
            if(f < IR_MIN_FREQUENCY || f > IR_MAX_FREQUENCY) fail(p, IrErrRange);
            else p->cur.frequency = f;
        }
        break;
    }
    case KeyDuty:
        parse_duty(p, v, &p->cur.duty_cycle_ppm);
        break;
    default:
        fail(p, IrErrSyntax);
        break;
    }
}

/* ---------- byte-level state machine ---------- */

static void end_line(IrParser* p) {
    p->state = StLineStart;
    p->line_len = 0;
    p->line_no++;
    p->key_len = 0;
    p->val_len = 0;
    p->key_id = KeyNone;
}

static bool is_key_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static void resolve_key(IrParser* p) {
    p->key[p->key_len] = '\0';
    p->key_id = KeyNone;
    for(size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if(strcmp(p->key, KEYS[i].text) == 0) {
            p->key_id = KEYS[i].id;
            break;
        }
    }
    if(p->key_id == KeyNone) {
        fail(p, IrErrUnsupportedKey);
        return;
    }
    if(p->stage == 0) {
        if(p->key_id != KeyFiletype) fail(p, IrErrFiletype);
    } else if(p->stage == 1) {
        if(p->key_id != KeyVersion) fail(p, IrErrVersion);
    } else if(p->key_id == KeyFiletype || p->key_id == KeyVersion) {
        fail(p, IrErrUnsupportedKey);
    } else if(p->key_id != KeyName) {
        if(!p->open) fail(p, IrErrKeyBeforeName);
        else if(p->seen & SB(p->key_id)) fail(p, IrErrDuplicateKey);
        else {
            p->seen |= SB(p->key_id);
            if(p->key_id == KeyData) {
                p->tok_len = 0;
                p->cur.timing_count = 0;
            }
        }
    }
}

static void feed_byte(IrParser* p, char c) {
    if(p->status != IrOk) return;
    if(c == '\r') return; /* CRLF tolerated; a lone CR is ignored */
    if(++p->total > IR_MAX_FILE) {
        fail(p, IrErrFileTooLarge);
        return;
    }
    if(c != '\n' && ++p->line_len > IR_MAX_LINE) {
        fail(p, IrErrLineTooLong);
        return;
    }

    switch(p->state) {
    case StLineStart:
        if(c == '\n') {
            if(p->stage < 2) fail(p, p->stage == 0 ? IrErrFiletype : IrErrVersion);
            else end_line(p);
        } else if(c == '#') {
            if(p->stage < 2) fail(p, p->stage == 0 ? IrErrFiletype : IrErrVersion);
            else p->state = StComment;
        } else if(is_key_char(c)) {
            p->key[0] = c;
            p->key_len = 1;
            p->state = StKey;
        } else {
            fail(p, IrErrSyntax);
        }
        break;

    case StComment:
        if(c == '\n') end_line(p);
        break;

    case StKey:
        if(c == ':') {
            resolve_key(p);
            p->state = StSpace;
        } else if(is_key_char(c) && p->key_len < IR_KEY_MAX) {
            p->key[p->key_len++] = c;
        } else {
            fail(p, IrErrSyntax);
        }
        break;

    case StSpace:
        if(c == ' ') break;
        p->state = StValue;
        p->val_len = 0;
        p->tok_len = 0;
        /* fall through */
    case StValue:
        if(c == '\n') {
            end_value(p);
            if(p->status == IrOk) end_line(p);
        } else if((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
            fail(p, IrErrBadChar);
        } else if(p->key_id == KeyData) {
            if(c == ' ') {
                finish_token(p);
            } else if(c >= '0' && c <= '9') {
                if(p->tok_len >= 10) fail(p, IrErrOverflow);
                else p->tok[p->tok_len++] = c;
            } else {
                fail(p, IrErrBadValue);
            }
        } else if(p->val_len >= IR_VALUE_MAX) {
            fail(p, p->key_id == KeyName ? IrErrBadName : IrErrBadValue);
        } else {
            p->val[p->val_len++] = c;
        }
        break;

    default:
        fail(p, IrErrSyntax);
        break;
    }
}

bool ir_parser_feed(IrParser* p, const char* data, size_t len) {
    for(size_t i = 0; i < len && p->status == IrOk; i++) feed_byte(p, data[i]);
    return p->status == IrOk;
}

bool ir_parser_finish(IrParser* p) {
    if(p->status != IrOk) return false;
    /* a final line without '\n' (or an unterminated key) */
    if(p->line_len > 0 || p->state != StLineStart) feed_byte(p, '\n');
    if(p->status != IrOk) return false;
    if(p->stage == 0) fail(p, IrErrFiletype);
    else if(p->stage == 1) fail(p, IrErrVersion);
    else {
        commit_signal(p);
        if(p->status == IrOk && p->count == 0) fail(p, IrErrTruncated);
    }
    return p->status == IrOk;
}

IrStatus ir_parse_buffer(IrParser* p, const char* data, size_t len) {
    ir_parser_init(p);
    if(ir_parser_feed(p, data, len)) ir_parser_finish(p);
    return p->status;
}

const char* ir_status_str(IrStatus s) {
    switch(s) {
    case IrOk: return "ok";
    case IrErrFiletype: return "missing or wrong Filetype";
    case IrErrVersion: return "unsupported or missing Version";
    case IrErrSyntax: return "syntax error";
    case IrErrBadChar: return "invalid character";
    case IrErrLineTooLong: return "line too long";
    case IrErrFileTooLarge: return "file too large";
    case IrErrUnsupportedKey: return "unsupported key";
    case IrErrDuplicateKey: return "duplicate key";
    case IrErrKeyBeforeName: return "field before name";
    case IrErrBadValue: return "bad value";
    case IrErrOverflow: return "number overflow";
    case IrErrRange: return "value out of range";
    case IrErrBadName: return "bad name";
    case IrErrTooManyTimings: return "too many timings";
    case IrErrTooManyButtons: return "too many buttons";
    case IrErrIncomplete: return "incomplete signal";
    case IrErrTruncated: return "no signals / truncated";
    }
    return "unknown";
}
