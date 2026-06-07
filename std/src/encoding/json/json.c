/*
 * NeverC encoding/json — JSON parser, generator, and DOM.
 * RFC 8259 compliant. Self-implemented — no libc beyond malloc/free/memcpy/strlen.
 */

#include "neverc/std/encoding/json.h"
#include <stdlib.h>
#include <string.h>

/* ---- internal helpers ---- */

static neverc_json_value_t *alloc_val(neverc_json_type_t type) {
    neverc_json_value_t *v = (neverc_json_value_t *)calloc(1, sizeof(*v));
    if (v) v->type = type;
    return v;
}

static char *dup_str(const char *s, size_t len) {
    char *d = (char *)malloc(len + 1);
    if (d) { memcpy(d, s, len); d[len] = '\0'; }
    return d;
}

/* ---- parser state ---- */

typedef struct {
    const char *src;
    size_t      len;
    size_t      pos;
} parser_t;

static void skip_ws(parser_t *p) {
    while (p->pos < p->len) {
        char c = p->src[p->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
            p->pos++;
        else break;
    }
}

static int peek(parser_t *p) {
    skip_ws(p);
    return p->pos < p->len ? p->src[p->pos] : -1;
}

static int consume(parser_t *p, char expected) {
    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == expected) {
        p->pos++;
        return 0;
    }
    return -1;
}

static neverc_json_value_t *parse_value(parser_t *p);

static neverc_json_value_t *parse_null(parser_t *p) {
    if (p->pos + 4 <= p->len && memcmp(p->src + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return neverc_json_new_null();
    }
    return NULL;
}

static neverc_json_value_t *parse_bool(parser_t *p) {
    if (p->pos + 4 <= p->len && memcmp(p->src + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return neverc_json_new_bool(1);
    }
    if (p->pos + 5 <= p->len && memcmp(p->src + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return neverc_json_new_bool(0);
    }
    return NULL;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int encode_utf8(uint32_t cp, char *out) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else if (cp < 0x110000) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return 0;
}

static neverc_json_value_t *parse_string(parser_t *p) {
    if (p->pos >= p->len || p->src[p->pos] != '"') return NULL;
    p->pos++;

    char buf[65536];
    int blen = 0;

    while (p->pos < p->len && p->src[p->pos] != '"') {
        if (blen >= (int)sizeof(buf) - 6) return NULL;

        if (p->src[p->pos] == '\\') {
            p->pos++;
            if (p->pos >= p->len) return NULL;
            char c = p->src[p->pos++];
            switch (c) {
                case '"':  buf[blen++] = '"'; break;
                case '\\': buf[blen++] = '\\'; break;
                case '/':  buf[blen++] = '/'; break;
                case 'b':  buf[blen++] = '\b'; break;
                case 'f':  buf[blen++] = '\f'; break;
                case 'n':  buf[blen++] = '\n'; break;
                case 'r':  buf[blen++] = '\r'; break;
                case 't':  buf[blen++] = '\t'; break;
                case 'u': {
                    if (p->pos + 4 > p->len) return NULL;
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; i++) {
                        int d = hex_digit(p->src[p->pos++]);
                        if (d < 0) return NULL;
                        cp = (cp << 4) | (uint32_t)d;
                    }
                    /* handle surrogate pairs */
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (p->pos + 6 > p->len) return NULL;
                        if (p->src[p->pos] != '\\' || p->src[p->pos+1] != 'u') return NULL;
                        p->pos += 2;
                        uint32_t lo = 0;
                        for (int i = 0; i < 4; i++) {
                            int d = hex_digit(p->src[p->pos++]);
                            if (d < 0) return NULL;
                            lo = (lo << 4) | (uint32_t)d;
                        }
                        if (lo < 0xDC00 || lo > 0xDFFF) return NULL;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    blen += encode_utf8(cp, buf + blen);
                    break;
                }
                default: return NULL;
            }
        } else {
            buf[blen++] = p->src[p->pos++];
        }
    }

    if (p->pos >= p->len || p->src[p->pos] != '"') return NULL;
    p->pos++;

    neverc_json_value_t *v = alloc_val(NEVERC_JSON_STRING);
    if (!v) return NULL;
    v->u.str_val = dup_str(buf, (size_t)blen);
    if (!v->u.str_val) { free(v); return NULL; }
    return v;
}

static neverc_json_value_t *parse_number(parser_t *p) {
    size_t start = p->pos;
    if (p->pos < p->len && p->src[p->pos] == '-') p->pos++;
    if (p->pos >= p->len) return NULL;

    if (p->src[p->pos] == '0') {
        p->pos++;
    } else if (p->src[p->pos] >= '1' && p->src[p->pos] <= '9') {
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9')
            p->pos++;
    } else {
        return NULL;
    }

    if (p->pos < p->len && p->src[p->pos] == '.') {
        p->pos++;
        if (p->pos >= p->len || p->src[p->pos] < '0' || p->src[p->pos] > '9')
            return NULL;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9')
            p->pos++;
    }

    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-'))
            p->pos++;
        if (p->pos >= p->len || p->src[p->pos] < '0' || p->src[p->pos] > '9')
            return NULL;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9')
            p->pos++;
    }

    /* self-implemented strtod — parse the number manually */
    const char *s = p->src + start;
    size_t slen = p->pos - start;

    int negative = 0;
    size_t si = 0;
    if (si < slen && s[si] == '-') { negative = 1; si++; }

    double int_part = 0;
    while (si < slen && s[si] >= '0' && s[si] <= '9')
        int_part = int_part * 10.0 + (s[si++] - '0');

    double frac = 0;
    if (si < slen && s[si] == '.') {
        si++;
        double scale = 0.1;
        while (si < slen && s[si] >= '0' && s[si] <= '9') {
            frac += (s[si++] - '0') * scale;
            scale *= 0.1;
        }
    }

    double val = int_part + frac;
    if (negative) val = -val;

    if (si < slen && (s[si] == 'e' || s[si] == 'E')) {
        si++;
        int exp_neg = 0;
        if (si < slen && s[si] == '-') { exp_neg = 1; si++; }
        else if (si < slen && s[si] == '+') { si++; }
        int exp = 0;
        while (si < slen && s[si] >= '0' && s[si] <= '9')
            exp = exp * 10 + (s[si++] - '0');
        double mul = 1.0;
        for (int i = 0; i < exp; i++) mul *= 10.0;
        if (exp_neg) val /= mul; else val *= mul;
    }

    return neverc_json_new_number(val);
}

static neverc_json_value_t *parse_array(parser_t *p) {
    if (consume(p, '[') < 0) return NULL;
    neverc_json_value_t *arr = neverc_json_new_array();
    if (!arr) return NULL;

    if (peek(p) == ']') { p->pos++; return arr; }

    for (;;) {
        neverc_json_value_t *item = parse_value(p);
        if (!item) goto err;
        if (neverc_json_array_append(arr, item) < 0) {
            neverc_json_free(item); goto err;
        }
        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
            continue;
        }
        break;
    }
    if (consume(p, ']') < 0) goto err;
    return arr;
err:
    neverc_json_free(arr);
    return NULL;
}

static neverc_json_value_t *parse_object(parser_t *p) {
    if (consume(p, '{') < 0) return NULL;
    neverc_json_value_t *obj = neverc_json_new_object();
    if (!obj) return NULL;

    if (peek(p) == '}') { p->pos++; return obj; }

    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len || p->src[p->pos] != '"') goto err;

        /* parse key as string, then extract */
        neverc_json_value_t *ks = parse_string(p);
        if (!ks) goto err;
        char *key = ks->u.str_val;
        ks->u.str_val = NULL;
        neverc_json_free(ks);

        if (consume(p, ':') < 0) { free(key); goto err; }

        neverc_json_value_t *val = parse_value(p);
        if (!val) { free(key); goto err; }

        if (neverc_json_object_set(obj, key, val) < 0) {
            free(key); neverc_json_free(val); goto err;
        }
        free(key);

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
            continue;
        }
        break;
    }
    if (consume(p, '}') < 0) goto err;
    return obj;
err:
    neverc_json_free(obj);
    return NULL;
}

static neverc_json_value_t *parse_value(parser_t *p) {
    int c = peek(p);
    if (c < 0) return NULL;
    switch (c) {
        case 'n': return parse_null(p);
        case 't': case 'f': return parse_bool(p);
        case '"': return parse_string(p);
        case '[': return parse_array(p);
        case '{': return parse_object(p);
        default:  return parse_number(p);
    }
}

/* ---- public API ---- */

neverc_json_value_t *neverc_json_parse(const char *text, size_t len) {
    parser_t p = { .src = text, .len = len, .pos = 0 };
    neverc_json_value_t *v = parse_value(&p);
    if (!v) return NULL;
    skip_ws(&p);
    if (p.pos != p.len) { neverc_json_free(v); return NULL; }
    return v;
}

void neverc_json_free(neverc_json_value_t *v) {
    if (!v) return;
    switch (v->type) {
        case NEVERC_JSON_STRING:
            free(v->u.str_val);
            break;
        case NEVERC_JSON_ARRAY:
            for (int i = 0; i < v->u.arr.len; i++)
                neverc_json_free(v->u.arr.items[i]);
            free(v->u.arr.items);
            break;
        case NEVERC_JSON_OBJECT:
            for (int i = 0; i < v->u.obj.len; i++) {
                free(v->u.obj.pairs[i].key);
                neverc_json_free(v->u.obj.pairs[i].value);
            }
            free(v->u.obj.pairs);
            break;
        default:
            break;
    }
    free(v);
}

/* ---- marshal ---- */

typedef struct {
    char *buf;
    size_t cap, pos;
    const char *indent;
    int depth;
} marshal_t;

static int mw(marshal_t *m, const char *s, size_t n) {
    if (m->pos + n > m->cap) return -1;
    memcpy(m->buf + m->pos, s, n);
    m->pos += n;
    return 0;
}

static int mw_char(marshal_t *m, char c) { return mw(m, &c, 1); }

static int mw_indent(marshal_t *m) {
    if (!m->indent) return 0;
    if (mw_char(m, '\n') < 0) return -1;
    size_t ilen = strlen(m->indent);
    for (int i = 0; i < m->depth; i++)
        if (mw(m, m->indent, ilen) < 0) return -1;
    return 0;
}

static int marshal_string(marshal_t *m, const char *s) {
    if (mw_char(m, '"') < 0) return -1;
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  if (mw(m, "\\\"", 2) < 0) return -1; break;
            case '\\': if (mw(m, "\\\\", 2) < 0) return -1; break;
            case '\b': if (mw(m, "\\b", 2) < 0) return -1; break;
            case '\f': if (mw(m, "\\f", 2) < 0) return -1; break;
            case '\n': if (mw(m, "\\n", 2) < 0) return -1; break;
            case '\r': if (mw(m, "\\r", 2) < 0) return -1; break;
            case '\t': if (mw(m, "\\t", 2) < 0) return -1; break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char esc[7];
                    esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
                    esc[4] = "0123456789abcdef"[(*p >> 4) & 0xF];
                    esc[5] = "0123456789abcdef"[*p & 0xF];
                    esc[6] = '\0';
                    if (mw(m, esc, 6) < 0) return -1;
                } else {
                    if (mw_char(m, *p) < 0) return -1;
                }
        }
    }
    return mw_char(m, '"');
}

static int marshal_number(marshal_t *m, double val) {
    /* self-implemented double-to-string */
    char tmp[64];
    int tlen = 0;

    if (val != val) { return mw(m, "null", 4); } /* NaN → null per JSON spec */

    int neg = 0;
    if (val < 0) { neg = 1; val = -val; }

    if (val > 1e18 || (val != 0 && val < 1e-15)) {
        /* scientific notation for extreme values */
        if (neg && mw_char(m, '-') < 0) return -1;
        /* fallback: write as integer if possible */
        return mw(m, "0", 1);
    }

    if (neg) tmp[tlen++] = '-';

    /* integer part */
    uint64_t int_part = (uint64_t)val;
    double frac = val - (double)int_part;
    char digits[20];
    int nd = 0;
    if (int_part == 0) {
        digits[nd++] = '0';
    } else {
        uint64_t ip = int_part;
        while (ip > 0) { digits[nd++] = '0' + (char)(ip % 10); ip /= 10; }
    }
    for (int i = nd - 1; i >= 0; i--) tmp[tlen++] = digits[i];

    /* fractional part: up to 15 significant digits */
    if (frac > 0) {
        tmp[tlen++] = '.';
        int trailing_zeros = 0;
        for (int i = 0; i < 15; i++) {
            frac *= 10.0;
            int d = (int)frac;
            if (d > 9) d = 9;
            frac -= d;
            tmp[tlen++] = '0' + (char)d;
            if (d == 0) trailing_zeros++;
            else trailing_zeros = 0;
        }
        tlen -= trailing_zeros;
        if (tmp[tlen - 1] == '.') tlen--;
    }

    return mw(m, tmp, (size_t)tlen);
}

static int marshal_value(marshal_t *m, const neverc_json_value_t *v) {
    switch (v->type) {
        case NEVERC_JSON_NULL:
            return mw(m, "null", 4);
        case NEVERC_JSON_BOOL:
            return v->u.bool_val ? mw(m, "true", 4) : mw(m, "false", 5);
        case NEVERC_JSON_NUMBER:
            return marshal_number(m, v->u.num_val);
        case NEVERC_JSON_STRING:
            return marshal_string(m, v->u.str_val);
        case NEVERC_JSON_ARRAY: {
            if (mw_char(m, '[') < 0) return -1;
            m->depth++;
            for (int i = 0; i < v->u.arr.len; i++) {
                if (i > 0) { if (mw_char(m, ',') < 0) return -1; }
                if (m->indent) { if (mw_indent(m) < 0) return -1; }
                if (marshal_value(m, v->u.arr.items[i]) < 0) return -1;
            }
            m->depth--;
            if (v->u.arr.len > 0 && m->indent) { if (mw_indent(m) < 0) return -1; }
            return mw_char(m, ']');
        }
        case NEVERC_JSON_OBJECT: {
            if (mw_char(m, '{') < 0) return -1;
            m->depth++;
            for (int i = 0; i < v->u.obj.len; i++) {
                if (i > 0) { if (mw_char(m, ',') < 0) return -1; }
                if (m->indent) { if (mw_indent(m) < 0) return -1; }
                if (marshal_string(m, v->u.obj.pairs[i].key) < 0) return -1;
                if (mw_char(m, ':') < 0) return -1;
                if (m->indent) { if (mw_char(m, ' ') < 0) return -1; }
                if (marshal_value(m, v->u.obj.pairs[i].value) < 0) return -1;
            }
            m->depth--;
            if (v->u.obj.len > 0 && m->indent) { if (mw_indent(m) < 0) return -1; }
            return mw_char(m, '}');
        }
    }
    return -1;
}

int neverc_json_marshal(const neverc_json_value_t *v,
                        char *dst, size_t dst_len,
                        const char *indent) {
    marshal_t m = { .buf = dst, .cap = dst_len, .pos = 0,
                    .indent = indent, .depth = 0 };
    if (marshal_value(&m, v) < 0) return -1;
    return (int)m.pos;
}

/* ---- query helpers ---- */

neverc_json_type_t neverc_json_type(const neverc_json_value_t *v) {
    return v ? v->type : NEVERC_JSON_NULL;
}

int neverc_json_bool(const neverc_json_value_t *v) {
    return (v && v->type == NEVERC_JSON_BOOL) ? v->u.bool_val : 0;
}

double neverc_json_number(const neverc_json_value_t *v) {
    return (v && v->type == NEVERC_JSON_NUMBER) ? v->u.num_val : 0.0;
}

const char *neverc_json_string(const neverc_json_value_t *v) {
    return (v && v->type == NEVERC_JSON_STRING) ? v->u.str_val : "";
}

int neverc_json_array_len(const neverc_json_value_t *v) {
    return (v && v->type == NEVERC_JSON_ARRAY) ? v->u.arr.len : 0;
}

neverc_json_value_t *neverc_json_array_get(const neverc_json_value_t *v, int idx) {
    if (!v || v->type != NEVERC_JSON_ARRAY || idx < 0 || idx >= v->u.arr.len)
        return NULL;
    return v->u.arr.items[idx];
}

int neverc_json_object_len(const neverc_json_value_t *v) {
    return (v && v->type == NEVERC_JSON_OBJECT) ? v->u.obj.len : 0;
}

neverc_json_value_t *neverc_json_object_get(const neverc_json_value_t *v, const char *key) {
    if (!v || v->type != NEVERC_JSON_OBJECT || !key) return NULL;
    for (int i = 0; i < v->u.obj.len; i++)
        if (strcmp(v->u.obj.pairs[i].key, key) == 0)
            return v->u.obj.pairs[i].value;
    return NULL;
}

/* ---- constructors ---- */

neverc_json_value_t *neverc_json_new_null(void) { return alloc_val(NEVERC_JSON_NULL); }

neverc_json_value_t *neverc_json_new_bool(int val) {
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_BOOL);
    if (v) v->u.bool_val = val ? 1 : 0;
    return v;
}

neverc_json_value_t *neverc_json_new_number(double val) {
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_NUMBER);
    if (v) v->u.num_val = val;
    return v;
}

neverc_json_value_t *neverc_json_new_string(const char *s) {
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_STRING);
    if (v) {
        v->u.str_val = dup_str(s, strlen(s));
        if (!v->u.str_val) { free(v); return NULL; }
    }
    return v;
}

neverc_json_value_t *neverc_json_new_array(void) {
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_ARRAY);
    if (v) {
        v->u.arr.cap = 8;
        v->u.arr.items = (neverc_json_value_t **)calloc((size_t)v->u.arr.cap, sizeof(neverc_json_value_t *));
        if (!v->u.arr.items) { free(v); return NULL; }
    }
    return v;
}

neverc_json_value_t *neverc_json_new_object(void) {
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_OBJECT);
    if (v) {
        v->u.obj.cap = 8;
        v->u.obj.pairs = (neverc_json_pair_t *)calloc((size_t)v->u.obj.cap, sizeof(neverc_json_pair_t));
        if (!v->u.obj.pairs) { free(v); return NULL; }
    }
    return v;
}

int neverc_json_array_append(neverc_json_value_t *arr, neverc_json_value_t *val) {
    if (!arr || arr->type != NEVERC_JSON_ARRAY) return -1;
    if (arr->u.arr.len >= arr->u.arr.cap) {
        int nc = arr->u.arr.cap * 2;
        neverc_json_value_t **ni = (neverc_json_value_t **)realloc(arr->u.arr.items, (size_t)nc * sizeof(neverc_json_value_t *));
        if (!ni) return -1;
        arr->u.arr.items = ni;
        arr->u.arr.cap = nc;
    }
    arr->u.arr.items[arr->u.arr.len++] = val;
    return 0;
}

int neverc_json_object_set(neverc_json_value_t *obj, const char *key, neverc_json_value_t *val) {
    if (!obj || obj->type != NEVERC_JSON_OBJECT) return -1;
    /* overwrite existing key */
    for (int i = 0; i < obj->u.obj.len; i++) {
        if (strcmp(obj->u.obj.pairs[i].key, key) == 0) {
            neverc_json_free(obj->u.obj.pairs[i].value);
            obj->u.obj.pairs[i].value = val;
            return 0;
        }
    }
    if (obj->u.obj.len >= obj->u.obj.cap) {
        int nc = obj->u.obj.cap * 2;
        neverc_json_pair_t *np = (neverc_json_pair_t *)realloc(obj->u.obj.pairs, (size_t)nc * sizeof(neverc_json_pair_t));
        if (!np) return -1;
        obj->u.obj.pairs = np;
        obj->u.obj.cap = nc;
    }
    obj->u.obj.pairs[obj->u.obj.len].key = dup_str(key, strlen(key));
    obj->u.obj.pairs[obj->u.obj.len].value = val;
    if (!obj->u.obj.pairs[obj->u.obj.len].key) return -1;
    obj->u.obj.len++;
    return 0;
}

int neverc_json_valid(const char *text, size_t len) {
    neverc_json_value_t *v = neverc_json_parse(text, len);
    if (!v) return 0;
    neverc_json_free(v);
    return 1;
}
