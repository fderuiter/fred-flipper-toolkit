#include "ir_parse.h"

#include <string.h>

#define READ_CHUNK 128
#define MAX_KEY_LEN 16

typedef enum {
    KeyFiletype,
    KeyVersion,
    KeyName,
    KeyType,
    KeyProtocol,
    KeyAddress,
    KeyCommand,
    KeyFrequency,
    KeyDutyCycle,
    KeyData,
    KeyUnknown,
} KeyId;

typedef enum {
    StLineStart,
    StComment,
    StKey,
    StPreValue,
    StValue,
    StData,
} State;

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

typedef struct {
    State state;
    IrStatus err;
    uint32_t line;
    int header_stage; /* 0: want Filetype, 1: want Version, 2: done */

    char key[MAX_KEY_LEN + 1];
    size_t key_len;
    KeyId key_id;

    char val[IR_MAX_VALUE_LEN + 1];
    size_t val_len;
    bool val_truncated; /* only used for name */

    uint64_t tok;
    uint8_t tok_digits;
    bool in_tok;
    uint16_t data_count;

    bool in_record;
    unsigned seen;
    IrButton cur;

    IrButton* out;
    size_t capacity;
    size_t count;
} Parser;

static const struct {
    const char* text;
    KeyId id;
} KEYS[] = {
    {"Filetype", KeyFiletype},
    {"Version", KeyVersion},
    {"name", KeyName},
    {"type", KeyType},
    {"protocol", KeyProtocol},
    {"address", KeyAddress},
    {"command", KeyCommand},
    {"frequency", KeyFrequency},
    {"duty_cycle", KeyDutyCycle},
    {"data", KeyData},
};

static bool fail(Parser* p, IrStatus s) {
    if(p->err == IrOk) p->err = s;
    return false;
}

static KeyId lookup_key(const char* key) {
    for(size_t i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) {
        if(strcmp(key, KEYS[i].text) == 0) return KEYS[i].id;
    }
    return KeyUnknown;
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* Decimal u32, 1..10 digits, no sign, no whitespace. */
static bool parse_u32(const char* s, size_t len, uint32_t* out) {
    if(len == 0 || len > 10) return false;
    uint64_t v = 0;
    for(size_t i = 0; i < len; i++) {
        if(!is_digit(s[i])) return false;
        v = v * 10 + (uint64_t)(s[i] - '0');
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

/* Exactly "XX XX XX XX". */
static bool parse_hex4(const char* s, size_t len, uint8_t out[4]) {
    if(len != 11) return false;
    for(size_t i = 0; i < 4; i++) {
        const char* t = s + i * 3;
        int hi = hex_nibble(t[0]);
        int lo = hex_nibble(t[1]);
        if(hi < 0 || lo < 0) return false;
        if(i < 3 && t[2] != ' ') return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* "0", "1", "0.33", "0.330000", "1.0": result in permille, must be 1..1000. */
static bool parse_duty(const char* s, size_t len, uint16_t* out) {
    if(len < 1 || (s[0] != '0' && s[0] != '1')) return false;
    uint32_t micro = (uint32_t)(s[0] - '0') * 1000000U;
    size_t i = 1;
    if(i < len) {
        if(s[i] != '.') return false;
        i++;
        uint32_t scale = 100000U;
        for(; i < len; i++) {
            if(!is_digit(s[i])) return false;
            if(scale) {
                micro += (uint32_t)(s[i] - '0') * scale;
                scale /= 10;
            }
        }
    }
    if(micro > 1000000U) return false;
    uint32_t permille = (micro + 500U) / 1000U;
    if(permille < 1 || permille > 1000) return false;
    *out = (uint16_t)permille;
    return true;
}

static bool is_proto_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || is_digit(c) || c == '_' || c == '-';
}

static bool finalize_record(Parser* p) {
    if(!p->in_record) return true;
    p->in_record = false;
    unsigned need;
    if(!(p->seen & SeenType)) {
        return fail(p, IrErrMissingField);
    } else if(p->cur.type == IrTypeParsed) {
        need = SeenProtocol | SeenAddress | SeenCommand;
    } else {
        need = SeenFrequency | SeenDuty | SeenData;
    }
    if((p->seen & need) != need) return fail(p, IrErrMissingField);
    if(p->count >= p->capacity) return fail(p, IrErrTooManyButtons);
    p->out[p->count++] = p->cur;
    return true;
}

/* Called when a "key:" has been read. Checks header order, record context, duplicates. */
static bool begin_key(Parser* p) {
    KeyId id = p->key_id;
    if(p->header_stage == 0) {
        if(id != KeyFiletype) return fail(p, IrErrBadFiletype);
        return true;
    }
    if(p->header_stage == 1) {
        if(id != KeyVersion) return fail(p, IrErrBadVersion);
        return true;
    }
    if(id == KeyUnknown || id == KeyFiletype || id == KeyVersion) {
        return fail(p, IrErrUnsupportedKey);
    }
    if(id == KeyName) {
        if(!finalize_record(p)) return false;
        memset(&p->cur, 0, sizeof(p->cur));
        p->in_record = true;
        p->seen = SeenName;
        p->val_truncated = false;
        return true;
    }
    if(!p->in_record) return fail(p, IrErrSyntax);

    unsigned bit = 0;
    switch(id) {
    case KeyType: bit = SeenType; break;
    case KeyProtocol: bit = SeenProtocol; break;
    case KeyAddress: bit = SeenAddress; break;
    case KeyCommand: bit = SeenCommand; break;
    case KeyFrequency: bit = SeenFrequency; break;
    case KeyDutyCycle: bit = SeenDuty; break;
    case KeyData: bit = SeenData; break;
    default: break;
    }
    if(p->seen & bit) return fail(p, IrErrDuplicateKey);
    if(id != KeyType) {
        if(!(p->seen & SeenType)) return fail(p, IrErrKeyOrder);
        bool parsed_key = (id == KeyProtocol || id == KeyAddress || id == KeyCommand);
        if(parsed_key != (p->cur.type == IrTypeParsed)) return fail(p, IrErrKeyOrder);
    }
    p->seen |= bit;
    return true;
}

/* A buffered (non-data) value is complete. */
static bool end_value(Parser* p) {
    while(p->val_len > 0 && p->val[p->val_len - 1] == ' ') p->val_len--;
    p->val[p->val_len] = '\0';
    const char* v = p->val;
    size_t n = p->val_len;
    IrButton* b = &p->cur;

    switch(p->key_id) {
    case KeyFiletype:
        if(strcmp(v, "IR signals file") != 0) return fail(p, IrErrBadFiletype);
        p->header_stage = 1;
        return true;
    case KeyVersion:
        if(strcmp(v, "1") != 0) return fail(p, IrErrBadVersion);
        p->header_stage = 2;
        return true;
    case KeyName:
        if(n == 0) return fail(p, IrErrBadValue);
        memcpy(b->name, v, n > IR_MAX_NAME_LEN ? IR_MAX_NAME_LEN : n);
        b->name[n > IR_MAX_NAME_LEN ? IR_MAX_NAME_LEN : n] = '\0';
        b->name_truncated = p->val_truncated || n > IR_MAX_NAME_LEN;
        return true;
    case KeyType:
        if(strcmp(v, "parsed") == 0) b->type = IrTypeParsed;
        else if(strcmp(v, "raw") == 0) b->type = IrTypeRaw;
        else return fail(p, IrErrBadValue);
        return true;
    case KeyProtocol:
        if(n == 0 || n > IR_MAX_PROTOCOL_LEN) return fail(p, IrErrBadValue);
        for(size_t i = 0; i < n; i++) {
            if(!is_proto_char(v[i])) return fail(p, IrErrBadValue);
        }
        memcpy(b->protocol, v, n);
        b->protocol[n] = '\0';
        return true;
    case KeyAddress:
        return parse_hex4(v, n, b->address) || fail(p, IrErrBadValue);
    case KeyCommand:
        return parse_hex4(v, n, b->command) || fail(p, IrErrBadValue);
    case KeyFrequency:
        if(!parse_u32(v, n, &b->frequency) || b->frequency < IR_MIN_FREQUENCY ||
           b->frequency > IR_MAX_FREQUENCY) {
            return fail(p, IrErrBadValue);
        }
        return true;
    case KeyDutyCycle:
        return parse_duty(v, n, &b->duty_permille) || fail(p, IrErrBadValue);
    default:
        return fail(p, IrErrSyntax);
    }
}

static bool flush_token(Parser* p) {
    if(!p->in_tok) return true;
    p->in_tok = false;
    if(p->tok == 0) return fail(p, IrErrBadValue);
    if(p->data_count >= IR_MAX_TIMINGS) return fail(p, IrErrTooManyTimings);
    p->data_count++;
    return true;
}

static bool end_data(Parser* p) {
    if(!flush_token(p)) return false;
    if(p->data_count == 0) return fail(p, IrErrBadValue);
    p->cur.timing_count = p->data_count;
    return true;
}

/* Newline or EOF reached while in a key/value state. */
static bool end_line(Parser* p) {
    switch(p->state) {
    case StKey: return fail(p, IrErrSyntax);
    case StPreValue:
        p->val_len = 0;
        return end_value(p);
    case StValue: return end_value(p);
    case StData: return end_data(p);
    default: return true;
    }
}

static bool feed(Parser* p, uint8_t c) {
    if(p->state == StComment) {
        if(c == '\n') {
            p->state = StLineStart;
            p->line++;
        }
        return true;
    }
    if(c == '\r') return true;
    if(c != '\n' && (c < 0x20 || c == 0x7F)) return fail(p, IrErrBadCharacter);

    if(c == '\n') {
        if(!end_line(p)) return false;
        p->state = StLineStart;
        p->line++;
        return true;
    }

    switch(p->state) {
    case StLineStart:
        if(c == '#') {
            p->state = StComment;
            return true;
        }
        p->state = StKey;
        p->key_len = 0;
        /* fallthrough */
    case StKey:
        if(c == ':') {
            p->key[p->key_len] = '\0';
            p->key_id = lookup_key(p->key);
            if(!begin_key(p)) return false;
            p->val_len = 0;
            p->val_truncated = (p->key_id == KeyName) ? false : p->val_truncated;
            if(p->key_id == KeyData) {
                p->state = StData;
                p->in_tok = false;
                p->tok = 0;
                p->tok_digits = 0;
                p->data_count = 0;
            } else {
                p->state = StPreValue;
            }
            return true;
        }
        if(!is_proto_char(c)) return fail(p, IrErrSyntax);
        if(p->key_len >= MAX_KEY_LEN) return fail(p, IrErrLineTooLong);
        p->key[p->key_len++] = (char)c;
        return true;
    case StPreValue:
        if(c == ' ') return true;
        p->state = StValue;
        /* fallthrough */
    case StValue:
        if(c >= 0x80) return fail(p, IrErrBadCharacter);
        if(p->val_len < IR_MAX_VALUE_LEN) {
            p->val[p->val_len++] = (char)c;
            return true;
        }
        if(p->key_id == KeyName) {
            p->val_truncated = true; /* drop the excess, keep scanning */
            return true;
        }
        return fail(p, IrErrLineTooLong);
    case StData:
        if(is_digit((char)c)) {
            if(p->tok_digits >= 10) return fail(p, IrErrBadValue);
            p->tok = p->tok * 10 + (uint64_t)(c - '0');
            if(p->tok > UINT32_MAX) return fail(p, IrErrBadValue);
            p->tok_digits++;
            p->in_tok = true;
            return true;
        }
        if(c == ' ') {
            if(!flush_token(p)) return false;
            p->tok = 0;
            p->tok_digits = 0;
            return true;
        }
        return fail(p, IrErrBadValue);
    default:
        return fail(p, IrErrSyntax);
    }
}

IrResult ir_parse(IrReadFn read, void* ctx, IrButton* buttons, size_t capacity, size_t* count) {
    Parser p;
    memset(&p, 0, sizeof(p));
    p.state = StLineStart;
    p.line = 1;
    p.out = buttons;
    p.capacity = capacity;

    uint8_t chunk[READ_CHUNK];
    size_t total = 0;
    bool ok = true;

    while(ok) {
        int32_t n = read(ctx, chunk, sizeof(chunk));
        if(n < 0 || (size_t)n > sizeof(chunk)) {
            fail(&p, IrErrIo);
            break;
        }
        if(n == 0) break;
        if((size_t)n > IR_MAX_FILE_BYTES - total) {
            fail(&p, IrErrFileTooLarge);
            break;
        }
        for(int32_t i = 0; i < n && ok; i++) {
            ok = feed(&p, chunk[i]);
        }
        total += (size_t)n;
    }

    if(ok) {
        if(total == 0) {
            fail(&p, IrErrEmptyFile);
        } else if(end_line(&p)) {
            if(p.header_stage == 0) fail(&p, IrErrBadFiletype);
            else if(p.header_stage == 1) fail(&p, IrErrBadVersion);
            else finalize_record(&p);
        }
    }

    if(count) *count = p.count;
    IrResult r = {p.err, p.err == IrOk ? 0 : p.line};
    return r;
}

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
} MemSource;

static int32_t mem_read(void* ctx, uint8_t* buf, size_t max) {
    MemSource* m = ctx;
    size_t n = m->len - m->pos;
    if(n > max) n = max;
    if(n > 0) memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return (int32_t)n;
}

IrResult ir_parse_buffer(const uint8_t* data, size_t len, IrButton* buttons, size_t capacity, size_t* count) {
    MemSource m = {data, len, 0};
    return ir_parse(mem_read, &m, buttons, capacity, count);
}

const char* ir_status_string(IrStatus status) {
    switch(status) {
    case IrOk: return "ok";
    case IrErrIo: return "read error";
    case IrErrEmptyFile: return "empty file";
    case IrErrFileTooLarge: return "file too large";
    case IrErrBadFiletype: return "not an IR signals file";
    case IrErrBadVersion: return "unsupported version";
    case IrErrBadCharacter: return "invalid character";
    case IrErrSyntax: return "syntax error";
    case IrErrLineTooLong: return "line too long";
    case IrErrUnsupportedKey: return "unsupported key";
    case IrErrDuplicateKey: return "duplicate key";
    case IrErrKeyOrder: return "key out of order";
    case IrErrBadValue: return "bad value";
    case IrErrMissingField: return "missing field";
    case IrErrTooManyTimings: return "too many timings";
    case IrErrTooManyButtons: return "too many buttons";
    }
    return "unknown";
}
