/*
 * NeverC encoding/json — JSON parser, generator, and DOM.
 * RFC 8259 compliant. Self-implemented — no libc beyond malloc/free/memcpy/strlen.
 */

#include "neverc/std/encoding/json.h"
#include "neverc/std/strconv.h"
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

/* Bound parser recursion so adversarial deep nesting ("[[[[..." / "{...") can't
 * overflow the C stack (a crash/DoS). 1000 is far beyond real-world JSON yet
 * stays safe even on small (≈512 KiB iOS/Windows) thread stacks. */
#define NCI_JSON_MAX_DEPTH 1000

typedef struct {
    const char *src;
    size_t      len;
    size_t      pos;
    int         depth;
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

/* Growable byte buffer that starts in a caller-provided stack array and spills
 * to the heap only when a string outgrows it. Replaces the old fixed 64 KiB
 * stack buffer, which both rejected any longer string (valid per RFC 8259) and
 * burned 64 KiB of stack on every parse_string call. */
typedef struct { char *p; size_t len, cap; char *stack; } sbuf_t;

static int sb_reserve(sbuf_t *b, size_t extra) {
    if (b->len + extra <= b->cap) return 0;
    size_t ncap = b->cap * 2;
    while (ncap < b->len + extra) ncap *= 2;
    char *nb = (b->p == b->stack) ? (char *)malloc(ncap)
                                  : (char *)realloc(b->p, ncap);
    if (!nb) return -1;
    if (b->p == b->stack) memcpy(nb, b->stack, b->len);
    b->p = nb; b->cap = ncap;
    return 0;
}

static void sb_free(sbuf_t *b) { if (b->p != b->stack) free(b->p); }

static neverc_json_value_t *parse_string(parser_t *p) {
    if (p->pos >= p->len || p->src[p->pos] != '"') return NULL;
    p->pos++;

    char stackbuf[256];
    sbuf_t b = { stackbuf, 0, sizeof stackbuf, stackbuf };

    for (;;) {
        if (p->pos >= p->len) { sb_free(&b); return NULL; }
        /* Bulk-copy the run of ordinary bytes up to the next '"' or '\\'. The
         * backslash search is bounded by the closing quote so a string early in
         * a big document never scans the whole remainder (which would be O(n^2)).
         * memchr is SIMD-accelerated, so escape-free strings copy in one shot. */
        const char *base = p->src + p->pos;
        size_t remain = p->len - p->pos;
        const char *q = (const char *)memchr(base, '"', remain);
        if (!q) { sb_free(&b); return NULL; }            /* unterminated */
        const char *bs = (const char *)memchr(base, '\\', (size_t)(q - base));
        const char *stop = bs ? bs : q;
        size_t run = (size_t)(stop - base);
        if (run) {
            if (sb_reserve(&b, run) < 0) { sb_free(&b); return NULL; }
            memcpy(b.p + b.len, base, run);
            b.len += run;
            p->pos += run;
        }
        if (stop == q) { p->pos++; break; }              /* closing quote */

        p->pos++;                                         /* consume '\\' */
        if (p->pos >= p->len) { sb_free(&b); return NULL; }
        char c = p->src[p->pos++];
        char out[4];
        int n = 1;
        switch (c) {
            case '"':  out[0] = '"';  break;
            case '\\': out[0] = '\\'; break;
            case '/':  out[0] = '/';  break;
            case 'b':  out[0] = '\b'; break;
            case 'f':  out[0] = '\f'; break;
            case 'n':  out[0] = '\n'; break;
            case 'r':  out[0] = '\r'; break;
            case 't':  out[0] = '\t'; break;
            case 'u': {
                if (p->pos + 4 > p->len) { sb_free(&b); return NULL; }
                uint32_t cp = 0;
                for (int i = 0; i < 4; i++) {
                    int d = hex_digit(p->src[p->pos++]);
                    if (d < 0) { sb_free(&b); return NULL; }
                    cp = (cp << 4) | (uint32_t)d;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {       /* high surrogate */
                    if (p->pos + 6 > p->len) { sb_free(&b); return NULL; }
                    if (p->src[p->pos] != '\\' || p->src[p->pos+1] != 'u') { sb_free(&b); return NULL; }
                    p->pos += 2;
                    uint32_t lo = 0;
                    for (int i = 0; i < 4; i++) {
                        int d = hex_digit(p->src[p->pos++]);
                        if (d < 0) { sb_free(&b); return NULL; }
                        lo = (lo << 4) | (uint32_t)d;
                    }
                    if (lo < 0xDC00 || lo > 0xDFFF) { sb_free(&b); return NULL; }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                n = encode_utf8(cp, out);
                break;
            }
            default: sb_free(&b); return NULL;
        }
        if (sb_reserve(&b, (size_t)n) < 0) { sb_free(&b); return NULL; }
        memcpy(b.p + b.len, out, (size_t)n);
        b.len += (size_t)n;
    }

    neverc_json_value_t *v = alloc_val(NEVERC_JSON_STRING);
    if (!v) { sb_free(&b); return NULL; }
    v->u.str_val = dup_str(b.p, b.len);
    sb_free(&b);
    if (!v->u.str_val) { free(v); return NULL; }
    return v;
}

static neverc_json_value_t *parse_number(parser_t *p) {
    size_t start = p->pos;
    int neg = 0;
    if (p->pos < p->len && p->src[p->pos] == '-') { p->pos++; neg = 1; }
    if (p->pos >= p->len) return NULL;

    size_t int_start = p->pos;
    if (p->src[p->pos] == '0') {
        p->pos++;
    } else if (p->src[p->pos] >= '1' && p->src[p->pos] <= '9') {
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9')
            p->pos++;
    } else {
        return NULL;
    }
    size_t int_digits = p->pos - int_start;
    int is_int = 1;                       /* no fraction or exponent yet */

    if (p->pos < p->len && p->src[p->pos] == '.') {
        is_int = 0;
        p->pos++;
        if (p->pos >= p->len || p->src[p->pos] < '0' || p->src[p->pos] > '9')
            return NULL;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9')
            p->pos++;
    }

    if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
        is_int = 0;
        p->pos++;
        if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-'))
            p->pos++;
        if (p->pos >= p->len || p->src[p->pos] < '0' || p->src[p->pos] > '9')
            return NULL;
        while (p->pos < p->len && p->src[p->pos] >= '0' && p->src[p->pos] <= '9')
            p->pos++;
    }

    /* Fast path: a plain integer with <= 15 digits is exactly representable as
     * a double (10^15 < 2^53), so fold the digits directly and skip both the
     * token copy and strconv's correctly-rounded float parser. Anything with a
     * fraction/exponent, or more digits (where rounding matters), falls back. */
    if (is_int && int_digits <= 15) {
        double val = 0.0;
        for (size_t i = int_start; i < int_start + int_digits; i++)
            val = val * 10.0 + (double)(p->src[i] - '0');
        return neverc_json_new_number(neg ? -val : val);
    }

    /* Hand the grammar-validated token to strconv's correctly-rounded parser. */
    const char *s = p->src + start;
    size_t slen = p->pos - start;
    char stackbuf[64];
    char *tok = (slen < sizeof stackbuf) ? stackbuf : (char *)malloc(slen + 1);
    if (!tok) return NULL;
    memcpy(tok, s, slen);
    tok[slen] = '\0';
    double val;
    int rc = neverc_strconv_parse_float(tok, &val);
    if (tok != stackbuf) free(tok);
    if (rc != NEVERC_STRCONV_OK && rc != NEVERC_STRCONV_ERR_RANGE) return NULL;

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

/* Open-addressing key index used only while parsing one object. The previous
 * parser called object_set per pair, whose linear duplicate-key scan made
 * parsing an n-key object O(n^2). This index makes duplicate detection O(1)
 * average, so parsing is linear, while keeping identical semantics (duplicate
 * keys collapse to one entry, last value wins, original position preserved). */
typedef struct {
    uint32_t *hashes;
    int      *slots;     /* pair index, or -1 for empty */
    int       cap;       /* power of two */
    int       cnt;
} keymap_t;

static uint32_t km_hash(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

static int km_init(keymap_t *km) {
    km->cap = 16; km->cnt = 0;
    km->hashes = (uint32_t *)calloc((size_t)km->cap, sizeof(uint32_t));
    km->slots  = (int *)malloc((size_t)km->cap * sizeof(int));
    if (!km->hashes || !km->slots) { free(km->hashes); free(km->slots); return -1; }
    for (int i = 0; i < km->cap; i++) km->slots[i] = -1;
    return 0;
}

static void km_free(keymap_t *km) { free(km->hashes); free(km->slots); }

/* Returns the existing pair index for key, or -1 if absent. */
static int km_lookup(const keymap_t *km, const neverc_json_value_t *obj,
                     const char *key, uint32_t hash) {
    int mask = km->cap - 1;
    int i = (int)(hash & (uint32_t)mask);
    while (km->slots[i] != -1) {
        if (km->hashes[i] == hash &&
            strcmp(obj->u.obj.pairs[km->slots[i]].key, key) == 0)
            return km->slots[i];
        i = (i + 1) & mask;
    }
    return -1;
}

static int km_grow(keymap_t *km) {
    int nc = km->cap * 2, mask = nc - 1;
    uint32_t *nh = (uint32_t *)calloc((size_t)nc, sizeof(uint32_t));
    int *ns = (int *)malloc((size_t)nc * sizeof(int));
    if (!nh || !ns) { free(nh); free(ns); return -1; }
    for (int i = 0; i < nc; i++) ns[i] = -1;
    for (int i = 0; i < km->cap; i++) {
        if (km->slots[i] == -1) continue;
        int j = (int)(km->hashes[i] & (uint32_t)mask);
        while (ns[j] != -1) j = (j + 1) & mask;
        nh[j] = km->hashes[i]; ns[j] = km->slots[i];
    }
    free(km->hashes); free(km->slots);
    km->hashes = nh; km->slots = ns; km->cap = nc;
    return 0;
}

static int km_insert(keymap_t *km, uint32_t hash, int pair_idx) {
    if ((km->cnt + 1) * 10 >= km->cap * 7) {     /* keep load factor < 0.7 */
        if (km_grow(km) < 0) return -1;
    }
    int mask = km->cap - 1;
    int i = (int)(hash & (uint32_t)mask);
    while (km->slots[i] != -1) i = (i + 1) & mask;
    km->hashes[i] = hash; km->slots[i] = pair_idx; km->cnt++;
    return 0;
}

/* Append a pair, taking ownership of key_owned (no extra dup). */
static int json_obj_append(neverc_json_value_t *obj, char *key_owned,
                           neverc_json_value_t *val) {
    if (obj->u.obj.len >= obj->u.obj.cap) {
        int nc = obj->u.obj.cap * 2;
        if (nc < 4) nc = 4;
        neverc_json_pair_t *np = (neverc_json_pair_t *)realloc(
            obj->u.obj.pairs, (size_t)nc * sizeof(neverc_json_pair_t));
        if (!np) return -1;
        obj->u.obj.pairs = np;
        obj->u.obj.cap = nc;
    }
    obj->u.obj.pairs[obj->u.obj.len].key = key_owned;
    obj->u.obj.pairs[obj->u.obj.len].value = val;
    obj->u.obj.len++;
    return 0;
}

static neverc_json_value_t *parse_object(parser_t *p) {
    if (consume(p, '{') < 0) return NULL;
    neverc_json_value_t *obj = neverc_json_new_object();
    if (!obj) return NULL;

    if (peek(p) == '}') { p->pos++; return obj; }

    keymap_t km;
    int have_km = (km_init(&km) == 0);

    for (;;) {
        skip_ws(p);
        if (p->pos >= p->len || p->src[p->pos] != '"') goto err;

        /* parse key as string, then take ownership of its buffer */
        neverc_json_value_t *ks = parse_string(p);
        if (!ks) goto err;
        char *key = ks->u.str_val;
        ks->u.str_val = NULL;
        neverc_json_free(ks);

        if (consume(p, ':') < 0) { free(key); goto err; }

        neverc_json_value_t *val = parse_value(p);
        if (!val) { free(key); goto err; }

        if (have_km) {
            uint32_t h = km_hash(key);
            int ex = km_lookup(&km, obj, key, h);
            if (ex >= 0) {                       /* duplicate key: last wins */
                neverc_json_free(obj->u.obj.pairs[ex].value);
                obj->u.obj.pairs[ex].value = val;
                free(key);
            } else {
                if (json_obj_append(obj, key, val) < 0 ||
                    km_insert(&km, h, obj->u.obj.len - 1) < 0) {
                    /* on failure the pair (if appended) is owned by obj/free */
                    if (obj->u.obj.len == 0 ||
                        obj->u.obj.pairs[obj->u.obj.len - 1].key != key) {
                        free(key); neverc_json_free(val);
                    }
                    goto err;
                }
            }
        } else {                                  /* OOM fallback: correct, O(n^2) */
            if (neverc_json_object_set(obj, key, val) < 0) {
                free(key); neverc_json_free(val); goto err;
            }
            free(key);
        }

        skip_ws(p);
        if (p->pos < p->len && p->src[p->pos] == ',') {
            p->pos++;
            continue;
        }
        break;
    }
    if (consume(p, '}') < 0) goto err;
    if (have_km) km_free(&km);
    return obj;
err:
    if (have_km) km_free(&km);
    neverc_json_free(obj);
    return NULL;
}

static neverc_json_value_t *parse_value(parser_t *p) {
    int c = peek(p);
    if (c < 0) return NULL;
    /* Recursion guard: parse_array/parse_object re-enter parse_value, so capping
     * it here bounds total nesting depth and prevents a stack-overflow DoS. */
    if (++p->depth > NCI_JSON_MAX_DEPTH) { p->depth--; return NULL; }
    neverc_json_value_t *v;
    switch (c) {
        case 'n': v = parse_null(p); break;
        case 't': case 'f': v = parse_bool(p); break;
        case '"': v = parse_string(p); break;
        case '[': v = parse_array(p); break;
        case '{': v = parse_object(p); break;
        default:  v = parse_number(p); break;
    }
    p->depth--;
    return v;
}

/* ---- public API ---- */

neverc_json_value_t *neverc_json_parse(const char *text, size_t len) {
    parser_t p = { .src = text, .len = len, .pos = 0, .depth = 0 };
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

/* A byte needs escaping iff it is '"', '\\', or a control char (< 0x20). All
 * other bytes — including UTF-8 lead/continuation bytes (>= 0x80) — are copied
 * verbatim. Used to bulk-copy the escape-free runs that dominate real strings,
 * instead of the old bounds-checked store per byte. */
static int json_needs_escape(unsigned char c) {
    return c < 0x20 || c == '"' || c == '\\';
}

static int marshal_string(marshal_t *m, const char *s) {
    if (mw_char(m, '"') < 0) return -1;
    const char *p = s;
    while (*p) {
        const char *run = p;
        while (*p && !json_needs_escape((unsigned char)*p)) p++;
        if (p > run && mw(m, run, (size_t)(p - run)) < 0) return -1;
        if (!*p) break;
        unsigned char c = (unsigned char)*p++;
        switch (c) {
            case '"':  if (mw(m, "\\\"", 2) < 0) return -1; break;
            case '\\': if (mw(m, "\\\\", 2) < 0) return -1; break;
            case '\b': if (mw(m, "\\b", 2) < 0) return -1; break;
            case '\f': if (mw(m, "\\f", 2) < 0) return -1; break;
            case '\n': if (mw(m, "\\n", 2) < 0) return -1; break;
            case '\r': if (mw(m, "\\r", 2) < 0) return -1; break;
            case '\t': if (mw(m, "\\t", 2) < 0) return -1; break;
            default: {                          /* other control char -> \u00XX */
                char esc[6];
                esc[0] = '\\'; esc[1] = 'u'; esc[2] = '0'; esc[3] = '0';
                esc[4] = "0123456789abcdef"[(c >> 4) & 0xF];
                esc[5] = "0123456789abcdef"[c & 0xF];
                if (mw(m, esc, 6) < 0) return -1;
            }
        }
    }
    return mw_char(m, '"');
}

static int marshal_number(marshal_t *m, double val) {
    /* NaN and +/-Inf are not valid JSON numbers -> emit null (per the spec). */
    if (val != val) return mw(m, "null", 4);        /* NaN */
    if (val - val != 0.0) return mw(m, "null", 4);  /* +/-Inf */

    /* Shortest correctly-rounded form (round-trips, matches encoding/json). */
    char tmp[40];
    int n = neverc_strconv_format_float(val, 'g', -1, tmp, sizeof tmp);
    if (n < 0) return mw(m, "null", 4);
    return mw(m, tmp, (size_t)n);
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
