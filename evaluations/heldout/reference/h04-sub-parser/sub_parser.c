// Reviewer-side reference solution for FRED held-out task H04. MIT licensed, original.
#include "sub_parser.h"
#include <string.h>

static const char* const presets[] = {
    "FuriHalSubGhzPresetOok270Async",
    "FuriHalSubGhzPresetOok650Async",
    "FuriHalSubGhzPreset2FSKDev238Async",
    "FuriHalSubGhzPreset2FSKDev476Async",
    "FuriHalSubGhzPresetMSK99_7KbAsync",
    "FuriHalSubGhzPresetGFSK9_99KbAsync",
};

const char* sub_error_str(SubError e) {
    static const char* const s[] = {
        "ok", "empty file", "wrong Filetype", "unsupported Version", "missing required key",
        "duplicate key", "unknown key", "invalid frequency", "frequency outside supported bands",
        "unsupported preset", "protocol is not RAW", "line too long", "malformed number",
        "duration out of range", "durations do not alternate", "too many timings",
        "no timings", "truncated file",
    };
    return (unsigned)e < sizeof(s) / sizeof(s[0]) ? s[e] : "?";
}

typedef struct {
    SubRead read;
    void* ctx;
    char buf[512];
    size_t pos, len;
    bool eof;
} Reader;

static int next_char(Reader* r) {
    if(r->pos == r->len) {
        if(r->eof) return -1;
        r->len = r->read(r->ctx, r->buf, sizeof(r->buf));
        r->pos = 0;
        if(r->len == 0) {
            r->eof = true;
            return -1;
        }
    }
    return (unsigned char)r->buf[r->pos++];
}

/* Reads one line into line[SUB_MAX_LINE + 1]. Returns 1 ok, 0 eof, -1 too long,
 * -2 last line without newline. */
static int read_line(Reader* r, char* line, size_t* n) {
    *n = 0;
    int c = next_char(r);
    if(c < 0) return 0;
    while(c >= 0 && c != '\n') {
        if(*n == SUB_MAX_LINE) {
            while(c >= 0 && c != '\n') c = next_char(r); /* drain, bounded memory */
            return -1;
        }
        line[(*n)++] = (char)c;
        c = next_char(r);
    }
    if(*n && line[*n - 1] == '\r') (*n)--;
    line[*n] = '\0';
    return c < 0 ? -2 : 1;
}

static bool parse_u32(const char* v, uint32_t* out) {
    uint64_t acc = 0;
    if(!*v) return false;
    for(; *v; v++) {
        if(*v < '0' || *v > '9') return false;
        acc = acc * 10u + (uint64_t)(*v - '0');
        if(acc > UINT32_MAX) return false;
    }
    *out = (uint32_t)acc;
    return true;
}

static bool in_band(uint32_t f) {
    return (f >= 300000000u && f <= 348000000u) || (f >= 387000000u && f <= 464000000u) ||
           (f >= 779000000u && f <= 928000000u);
}

enum { K_FREQ = 1, K_PRESET = 2, K_PROTO = 4 };

SubError sub_parse(SubRead read, void* ctx, SubSummary* out) {
    static char line[SUB_MAX_LINE + 1]; /* not reentrant; FAP uses one parser at a time */
    Reader r = {.read = read, .ctx = ctx};
    memset(out, 0, sizeof(*out));
    uint32_t seen = 0, line_no = 0, header = 0;
    int last_sign = 0;
    for(;;) {
        size_t n;
        int rc = read_line(&r, line, &n);
        if(rc == 0) break;
        line_no++;
        out->line = line_no;
        if(rc == -1) return SubErrLineTooLong;
        if(rc == -2) return SubErrTruncated; /* every valid line ends with a newline */
        if(n == 0 || line[0] == '#') continue;
        char* colon = strchr(line, ':');
        if(!colon || colon[1] != ' ') return SubErrToken;
        *colon = '\0';
        const char* key = line;
        char* val = colon + 2;
        if(header == 0) {
            if(strcmp(key, "Filetype") || strcmp(val, "Flipper SubGhz RAW File")) return SubErrFiletype;
            header = 1;
            continue;
        }
        if(header == 1) {
            if(strcmp(key, "Version") || strcmp(val, "1")) return SubErrVersion;
            header = 2;
            continue;
        }
        if(!strcmp(key, "Frequency")) {
            if(seen & K_FREQ) return SubErrDuplicateKey;
            seen |= K_FREQ;
            if(!parse_u32(val, &out->frequency_hz)) return SubErrFrequency;
            if(!in_band(out->frequency_hz)) return SubErrFrequencyBand;
        } else if(!strcmp(key, "Preset")) {
            if(seen & K_PRESET) return SubErrDuplicateKey;
            seen |= K_PRESET;
            bool ok = false;
            for(size_t i = 0; i < sizeof(presets) / sizeof(presets[0]); i++)
                if(!strcmp(val, presets[i])) ok = true;
            if(!ok) return SubErrPreset;
            size_t vl = strlen(val); /* fits: matched a known name shorter than preset[] */
            memcpy(out->preset, val, vl < sizeof(out->preset) ? vl + 1 : 0);
        } else if(!strcmp(key, "Protocol")) {
            if(seen & K_PROTO) return SubErrDuplicateKey;
            seen |= K_PROTO;
            if(strcmp(val, "RAW")) return SubErrProtocol;
        } else if(!strcmp(key, "RAW_Data")) {
            if((seen & (K_FREQ | K_PRESET | K_PROTO)) != (K_FREQ | K_PRESET | K_PROTO))
                return SubErrMissingKey;
            char* p = val;
            while(*p) {
                while(*p == ' ') p++;
                if(!*p) break;
                int sign = 1;
                if(*p == '-') {
                    sign = -1;
                    p++;
                }
                int64_t mag = 0;
                int digits = 0;
                while(*p >= '0' && *p <= '9') {
                    mag = mag * 10 + (*p - '0');
                    if(mag > SUB_MAX_DURATION) return SubErrDuration;
                    p++;
                    digits++;
                }
                if(!digits || (*p && *p != ' ')) return SubErrToken;
                if(mag == 0) return SubErrDuration;
                if(sign == last_sign) return SubErrSign;
                last_sign = sign;
                if(out->timings == SUB_MAX_TIMINGS) return SubErrTooManyTimings;
                out->timings++;
                out->total_us += (uint64_t)mag;
            }
        } else {
            return SubErrUnknownKey;
        }
    }
    out->line = 0;
    if(header == 0) return SubErrEmpty;
    if(header == 1) return SubErrVersion;
    if((seen & (K_FREQ | K_PRESET | K_PROTO)) != (K_FREQ | K_PRESET | K_PROTO)) return SubErrMissingKey;
    if(out->timings == 0) return SubErrNoTimings;
    return SubOk;
}
