/*
 * NeverC encoding/json — JSON parser, generator, and DOM.
 * RFC 8259 compliant. Self-implemented — no libc beyond malloc/free/memcpy/strlen.
 */

#include "neverc/std/encoding/json.h"
#include "neverc/std/strconv.h"
#include "../../hash/_wyhash_final3.h"
#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define NCI_JSON_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define NCI_JSON_NOINLINE __attribute__((noinline))
#else
#define NCI_JSON_NOINLINE
#endif

/* ---- internal helpers ---- */

typedef struct {
    char *owned_key;
    char *key_view;
    size_t key_len;
    neverc_json_value_t *value;
} json_pair_meta_t;

typedef struct json_owned_value {
    /* The released public object is the first member: its address and the
     * library allocation address are identical without changing public ABI. */
    neverc_json_value_t public_value;
    neverc_json_type_t owned_type;
    neverc_json_value_t *parent;
    char *owned_string;
    char *string_view;
    size_t string_len;
    void *public_storage;
    int private_len;
    int private_cap;
    union {
        neverc_json_value_t **array_values;
        json_pair_meta_t *object_pairs;
    } children;
    struct json_owned_value *registry_next;
} json_owned_value_t;

/* Public JSON structs can be supplied by old callers and are therefore not
 * allowed to contain a hidden cookie or trailer pointer. A private registry
 * identifies library allocations before any wrapper-only byte is read. */
static atomic_flag json_registry_lock_flag = ATOMIC_FLAG_INIT;
static json_owned_value_t *json_registry_initial_buckets[64];
static json_owned_value_t **json_registry_buckets =
    json_registry_initial_buckets;
static size_t json_registry_capacity =
    sizeof(json_registry_initial_buckets) /
    sizeof(json_registry_initial_buckets[0]);
static size_t json_registry_count;

static void json_registry_lock(void) {
    while (atomic_flag_test_and_set_explicit(
               &json_registry_lock_flag, memory_order_acquire)) {
    }
}

static void json_registry_unlock(void) {
    atomic_flag_clear_explicit(&json_registry_lock_flag, memory_order_release);
}

static size_t json_pointer_hash(const void *pointer) {
    uintptr_t value = (uintptr_t)pointer;
    value ^= value >> 7;
    value ^= value >> 17;
    value ^= value >> 3;
    return (size_t)value;
}

static json_owned_value_t *json_owned_find_locked(
    const neverc_json_value_t *value) {
    if (!value || json_registry_capacity == 0) return NULL;
    size_t bucket = json_pointer_hash(value) & (json_registry_capacity - 1U);
    for (json_owned_value_t *owned = json_registry_buckets[bucket]; owned;
         owned = owned->registry_next) {
        if (&owned->public_value == value) return owned;
    }
    return NULL;
}

static int json_registry_grow_locked(size_t capacity) {
    if (capacity < 64U) capacity = 64U;
    if (capacity > SIZE_MAX / sizeof(*json_registry_buckets)) return -1;
    json_owned_value_t **buckets = (json_owned_value_t **)calloc(
        capacity, sizeof(*buckets));
    if (!buckets) return -1;
    for (size_t i = 0; i < json_registry_capacity; i++) {
        json_owned_value_t *owned = json_registry_buckets[i];
        while (owned) {
            json_owned_value_t *next = owned->registry_next;
            size_t bucket = json_pointer_hash(&owned->public_value) &
                            (capacity - 1U);
            owned->registry_next = buckets[bucket];
            buckets[bucket] = owned;
            owned = next;
        }
    }
    if (json_registry_buckets != json_registry_initial_buckets)
        free(json_registry_buckets);
    json_registry_buckets = buckets;
    json_registry_capacity = capacity;
    return 0;
}

static int json_registry_add(json_owned_value_t *owned) {
    json_registry_lock();
    if (json_registry_count >= json_registry_capacity &&
        json_registry_capacity <= SIZE_MAX / 2U) {
        /* Growth is an optimization. If it fails, the existing chained table
         * remains correct and allocation can still succeed. */
        (void)json_registry_grow_locked(json_registry_capacity * 2U);
    }
    size_t bucket = json_pointer_hash(&owned->public_value) &
                    (json_registry_capacity - 1U);
    owned->registry_next = json_registry_buckets[bucket];
    json_registry_buckets[bucket] = owned;
    json_registry_count++;
    json_registry_unlock();
    return 0;
}

static json_owned_value_t *json_owned_find(
    const neverc_json_value_t *value) {
    json_registry_lock();
    json_owned_value_t *owned = json_owned_find_locked(value);
    json_registry_unlock();
    return owned;
}

static json_owned_value_t *json_registry_remove(
    neverc_json_value_t *value) {
    json_owned_value_t *found = NULL;
    json_registry_lock();
    if (value && json_registry_capacity != 0) {
        size_t bucket = json_pointer_hash(value) &
                        (json_registry_capacity - 1U);
        json_owned_value_t **link = &json_registry_buckets[bucket];
        while (*link && &(*link)->public_value != value)
            link = &(*link)->registry_next;
        if (*link) {
            found = *link;
            *link = found->registry_next;
            found->registry_next = NULL;
            json_registry_count--;
            if (json_registry_count == 0 &&
                json_registry_buckets != json_registry_initial_buckets) {
                free(json_registry_buckets);
                memset(json_registry_initial_buckets, 0,
                       sizeof(json_registry_initial_buckets));
                json_registry_buckets = json_registry_initial_buckets;
                json_registry_capacity =
                    sizeof(json_registry_initial_buckets) /
                    sizeof(json_registry_initial_buckets[0]);
            }
        }
    }
    json_registry_unlock();
    return found;
}

static neverc_json_value_t *alloc_val(neverc_json_type_t type) {
    json_owned_value_t *owned =
        (json_owned_value_t *)calloc(1, sizeof(*owned));
    if (!owned) return NULL;
    owned->public_value.type = type;
    owned->owned_type = type;
    if (json_registry_add(owned) != 0) {
        free(owned);
        return NULL;
    }
    return &owned->public_value;
}

static void json_free_single_allocation(neverc_json_value_t *value) {
    json_owned_value_t *owned = json_registry_remove(value);
    free(owned);
}

static char *dup_str(const char *s, size_t len) {
    if ((!s && len > 0) || len == SIZE_MAX) return NULL;
    char *d = (char *)malloc(len + 1U);
    if (d) {
        if (len > 0) memcpy(d, s, len);
        d[len] = '\0';
    }
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

/* Subtraction form: `pos + n` wraps when pos is near SIZE_MAX and would
 * treat an out-of-range read as in-bounds. */
static int parser_has(const parser_t *p, size_t n) {
    return p->pos <= p->len && n <= p->len - p->pos;
}

static int consume(parser_t *p, char expected) {
    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == expected) {
        p->pos++;
        return 0;
    }
    return -1;
}

static NCI_JSON_NOINLINE neverc_json_value_t *parse_value(parser_t *p);

static NCI_JSON_NOINLINE neverc_json_value_t *parse_null(parser_t *p) {
    if (parser_has(p, 4) && memcmp(p->src + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return neverc_json_new_null();
    }
    return NULL;
}

static NCI_JSON_NOINLINE neverc_json_value_t *parse_bool(parser_t *p) {
    if (parser_has(p, 4) && memcmp(p->src + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return neverc_json_new_bool(1);
    }
    if (parser_has(p, 5) && memcmp(p->src + p->pos, "false", 5) == 0) {
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
    if (cp >= 0xD800U && cp <= 0xDFFFU)
        return 0;
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

static int valid_utf8(const char *s, size_t len) {
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < len) {
        unsigned char c0 = p[i++];
        if (c0 < 0x80U) continue;
        if (c0 >= 0xC2U && c0 <= 0xDFU) {
            if (i >= len || (p[i] & 0xC0U) != 0x80U) return 0;
            i++;
            continue;
        }
        if (c0 >= 0xE0U && c0 <= 0xEFU) {
            if (len - i < 2U) return 0;
            unsigned char c1 = p[i], c2 = p[i + 1U];
            if ((c2 & 0xC0U) != 0x80U ||
                (c0 == 0xE0U ? (c1 < 0xA0U || c1 > 0xBFU) :
                 c0 == 0xEDU ? (c1 < 0x80U || c1 > 0x9FU) :
                               ((c1 & 0xC0U) != 0x80U)))
                return 0;
            i += 2U;
            continue;
        }
        if (c0 >= 0xF0U && c0 <= 0xF4U) {
            if (len - i < 3U) return 0;
            unsigned char c1 = p[i], c2 = p[i + 1U], c3 = p[i + 2U];
            if ((c2 & 0xC0U) != 0x80U || (c3 & 0xC0U) != 0x80U ||
                (c0 == 0xF0U ? (c1 < 0x90U || c1 > 0xBFU) :
                 c0 == 0xF4U ? (c1 < 0x80U || c1 > 0x8FU) :
                               ((c1 & 0xC0U) != 0x80U)))
                return 0;
            i += 3U;
            continue;
        }
        return 0;
    }
    return 1;
}

/* Growable byte buffer that starts in a caller-provided stack array and spills
 * to the heap only when a string outgrows it. Replaces the old fixed 64 KiB
 * stack buffer, which both rejected any longer string (valid per RFC 8259) and
 * burned 64 KiB of stack on every parse_string call. */
typedef struct { char *p; size_t len, cap; char *stack; } sbuf_t;

static int sb_reserve(sbuf_t *b, size_t extra) {
    if (b->len > b->cap || extra > SIZE_MAX - b->len)
        return -1;
    size_t needed = b->len + extra;
    if (needed <= b->cap) return 0;
    size_t ncap = b->cap ? b->cap : 256;
    while (ncap < needed) {
        if (ncap > SIZE_MAX / 2) {
            ncap = needed;
            break;
        }
        ncap *= 2;
    }
    char *nb = (b->p == b->stack) ? (char *)malloc(ncap)
                                  : (char *)realloc(b->p, ncap);
    if (!nb) return -1;
    if (b->p == b->stack) memcpy(nb, b->stack, b->len);
    b->p = nb; b->cap = ncap;
    return 0;
}

static void sb_free(sbuf_t *b) { if (b->p != b->stack) free(b->p); }

static NCI_JSON_NOINLINE neverc_json_value_t *parse_string(parser_t *p) {
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
            for (size_t i = 0; i < run; i++) {
                if ((unsigned char)base[i] < 0x20U) {
                    sb_free(&b);
                    return NULL;
                }
            }
            if (!valid_utf8(base, run)) { sb_free(&b); return NULL; }
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
                if (!parser_has(p, 4)) { sb_free(&b); return NULL; }
                uint32_t cp = 0;
                for (int i = 0; i < 4; i++) {
                    int d = hex_digit(p->src[p->pos++]);
                    if (d < 0) { sb_free(&b); return NULL; }
                    cp = (cp << 4) | (uint32_t)d;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {       /* high surrogate */
                    if (!parser_has(p, 6)) { sb_free(&b); return NULL; }
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
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    sb_free(&b);
                    return NULL;
                }
                n = encode_utf8(cp, out);
                if (n <= 0) { sb_free(&b); return NULL; }
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
    json_owned_value_t *owned = json_owned_find(v);
    if (owned) {
        owned->owned_string = v->u.str_val;
        owned->string_view = v->u.str_val;
        owned->string_len = b.len;
    }
    sb_free(&b);
    if (!v->u.str_val) { json_free_single_allocation(v); return NULL; }
    return v;
}

static NCI_JSON_NOINLINE neverc_json_value_t *parse_number(parser_t *p) {
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
        for (size_t i = int_start; i < p->pos; i++)
            val = val * 10.0 + (double)(p->src[i] - '0');
        return neverc_json_new_number(neg ? -val : val);
    }

    /* Hand the grammar-validated token to strconv's correctly-rounded parser. */
    const char *s = p->src + start;
    size_t slen = p->pos - start;
    char stackbuf[64];
    if (slen == SIZE_MAX) return NULL;
    char *tok = (slen < sizeof stackbuf) ? stackbuf : (char *)malloc(slen + 1);
    if (!tok) return NULL;
    memcpy(tok, s, slen);
    tok[slen] = '\0';
    double val;
    int rc = neverc_strconv_parse_float(tok, &val);
    if (tok != stackbuf) free(tok);
    if (rc != NEVERC_STRCONV_OK) {
        if (rc == NEVERC_STRCONV_ERR_RANGE && val == 0.0)
            return neverc_json_new_number(val);
        return NULL;
    }
    /* DOM stores numbers as finite double; overflow must not become Inf. */
    if (!isfinite(val))
        return NULL;

    return neverc_json_new_number(val);
}

static NCI_JSON_NOINLINE neverc_json_value_t *parse_array(parser_t *p) {
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

static size_t json_pair_key_length(const neverc_json_value_t *object,
                                   int index,
                                   const neverc_json_pair_t *pair) {
    json_owned_value_t *owned = json_owned_find(object);
    if (owned && owned->owned_type == NEVERC_JSON_OBJECT &&
        object->u.obj.pairs == owned->public_storage &&
        index >= 0 && index < owned->private_len) {
        const json_pair_meta_t *meta = &owned->children.object_pairs[index];
        if (pair->key == meta->key_view)
            return meta->key_len;
    }
    return pair->key ? strlen(pair->key) : 0;
}

/* The compact index intentionally stores final-v3's low 32 bits. km_lookup()
 * re-verifies length and bytes, so a hash collision cannot change semantics. */
static uint32_t km_hash(const char *s, size_t len) {
    return (uint32_t)nci_wyhash_final3(s, len, 0);
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
                     const char *key, size_t key_len, uint32_t hash) {
    int mask = km->cap - 1;
    int i = (int)(hash & (uint32_t)mask);
    while (km->slots[i] != -1) {
        const neverc_json_pair_t *pair =
            &obj->u.obj.pairs[km->slots[i]];
        size_t pair_len = json_pair_key_length(
            obj, km->slots[i], pair);
        if (km->hashes[i] == hash && pair_len == key_len &&
            (key_len == 0 || memcmp(pair->key, key, key_len) == 0))
            return km->slots[i];
        i = (i + 1) & mask;
    }
    return -1;
}

static int km_grow(keymap_t *km) {
    if (km->cap > INT_MAX / 2) return -1;
    int nc = km->cap * 2, mask = nc - 1;
    if ((size_t)nc > SIZE_MAX / sizeof(uint32_t) ||
        (size_t)nc > SIZE_MAX / sizeof(int))
        return -1;
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
    if ((int64_t)(km->cnt + 1) * 10 >=
        (int64_t)km->cap * 7) {                  /* keep load factor < 0.7 */
        if (km_grow(km) < 0) return -1;
    }
    int mask = km->cap - 1;
    int i = (int)(hash & (uint32_t)mask);
    while (km->slots[i] != -1) i = (i + 1) & mask;
    km->hashes[i] = hash; km->slots[i] = pair_idx; km->cnt++;
    return 0;
}

static int json_array_storage_valid(const neverc_json_value_t *array,
                                    const json_owned_value_t *owned) {
    if (!array || !owned || owned->owned_type != NEVERC_JSON_ARRAY ||
        array->type != NEVERC_JSON_ARRAY ||
        array->u.arr.items != owned->public_storage ||
        array->u.arr.len != owned->private_len ||
        array->u.arr.cap != owned->private_cap)
        return 0;
    return 1;
}

static int json_array_shape_valid(const neverc_json_value_t *array,
                                  const json_owned_value_t *owned) {
    if (!json_array_storage_valid(array, owned)) return 0;
    for (int i = 0; i < owned->private_len; i++) {
        if (array->u.arr.items[i] != owned->children.array_values[i])
            return 0;
    }
    return 1;
}

static int json_object_storage_valid(const neverc_json_value_t *object,
                                     const json_owned_value_t *owned) {
    if (!object || !owned || owned->owned_type != NEVERC_JSON_OBJECT ||
        object->type != NEVERC_JSON_OBJECT ||
        object->u.obj.pairs != owned->public_storage ||
        object->u.obj.len != owned->private_len ||
        object->u.obj.cap != owned->private_cap)
        return 0;
    return 1;
}

static int json_object_shape_valid(const neverc_json_value_t *object,
                                   const json_owned_value_t *owned) {
    if (!json_object_storage_valid(object, owned)) return 0;
    for (int i = 0; i < owned->private_len; i++) {
        const neverc_json_pair_t *pair = &object->u.obj.pairs[i];
        const json_pair_meta_t *meta = &owned->children.object_pairs[i];
        if (pair->key != meta->key_view || pair->value != meta->value)
            return 0;
    }
    return 1;
}

static int json_array_grow(neverc_json_value_t *array,
                           json_owned_value_t *owned, int capacity) {
    if (capacity <= owned->private_cap || capacity < owned->private_len ||
        (size_t)capacity > SIZE_MAX / sizeof(neverc_json_value_t *))
        return -1;
    neverc_json_value_t **items = (neverc_json_value_t **)calloc(
        (size_t)capacity, sizeof(*items));
    neverc_json_value_t **children = (neverc_json_value_t **)calloc(
        (size_t)capacity, sizeof(*children));
    if (!items || !children) {
        free(items);
        free(children);
        return -1;
    }
    if (owned->private_len > 0) {
        size_t bytes = (size_t)owned->private_len * sizeof(*items);
        memcpy(items, owned->public_storage, bytes);
        memcpy(children, owned->children.array_values, bytes);
    }
    free(owned->public_storage);
    free(owned->children.array_values);
    owned->public_storage = items;
    owned->children.array_values = children;
    owned->private_cap = capacity;
    array->u.arr.items = items;
    array->u.arr.cap = capacity;
    return 0;
}

static int json_object_grow(neverc_json_value_t *object,
                            json_owned_value_t *owned, int capacity) {
    if (capacity <= owned->private_cap || capacity < owned->private_len ||
        (size_t)capacity > SIZE_MAX / sizeof(neverc_json_pair_t) ||
        (size_t)capacity > SIZE_MAX / sizeof(json_pair_meta_t))
        return -1;
    neverc_json_pair_t *pairs = (neverc_json_pair_t *)calloc(
        (size_t)capacity, sizeof(*pairs));
    json_pair_meta_t *metadata = (json_pair_meta_t *)calloc(
        (size_t)capacity, sizeof(*metadata));
    if (!pairs || !metadata) {
        free(pairs);
        free(metadata);
        return -1;
    }
    if (owned->private_len > 0) {
        memcpy(pairs, owned->public_storage,
               (size_t)owned->private_len * sizeof(*pairs));
        memcpy(metadata, owned->children.object_pairs,
               (size_t)owned->private_len * sizeof(*metadata));
    }
    free(owned->public_storage);
    free(owned->children.object_pairs);
    owned->public_storage = pairs;
    owned->children.object_pairs = metadata;
    owned->private_cap = capacity;
    object->u.obj.pairs = pairs;
    object->u.obj.cap = capacity;
    return 0;
}

/* Append a pair, taking ownership of key_owned (no extra dup). */
static int json_obj_append(neverc_json_value_t *obj, char *key_owned,
                           size_t key_len, neverc_json_value_t *val) {
    json_owned_value_t *object_owned = json_owned_find(obj);
    json_owned_value_t *value_owned = json_owned_find(val);
    if (!json_object_storage_valid(obj, object_owned) || !value_owned ||
        value_owned->parent)
        return -1;
    if (object_owned->private_len >= object_owned->private_cap) {
        if (object_owned->private_cap > INT_MAX / 2) return -1;
        int capacity = object_owned->private_cap == 0
            ? 4 : object_owned->private_cap * 2;
        if (json_object_grow(obj, object_owned, capacity) != 0) return -1;
    }
    int index = object_owned->private_len;
    obj->u.obj.pairs[index].key = key_owned;
    obj->u.obj.pairs[index].value = val;
    object_owned->children.object_pairs[index].owned_key = key_owned;
    object_owned->children.object_pairs[index].key_view = key_owned;
    object_owned->children.object_pairs[index].key_len = key_len;
    object_owned->children.object_pairs[index].value = val;
    object_owned->private_len++;
    obj->u.obj.len = object_owned->private_len;
    value_owned->parent = obj;
    return 0;
}

static NCI_JSON_NOINLINE neverc_json_value_t *parse_object(parser_t *p) {
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
        json_owned_value_t *key_owned = json_owned_find(ks);
        size_t key_len = key_owned ? key_owned->string_len : strlen(key);
        ks->u.str_val = NULL;
        if (key_owned) {
            key_owned->owned_string = NULL;
            key_owned->string_view = NULL;
            key_owned->string_len = 0;
        }
        neverc_json_free(ks);

        if (consume(p, ':') < 0) { free(key); goto err; }

        neverc_json_value_t *val = parse_value(p);
        if (!val) { free(key); goto err; }

        if (have_km) {
            uint32_t h = km_hash(key, key_len);
            int ex = km_lookup(&km, obj, key, key_len, h);
            if (ex >= 0) {                       /* duplicate key: last wins */
                json_owned_value_t *object_owned = json_owned_find(obj);
                neverc_json_value_t *old = obj->u.obj.pairs[ex].value;
                json_owned_value_t *old_owned = json_owned_find(old);
                json_owned_value_t *value_owned = json_owned_find(val);
                if (!object_owned || !old_owned || !value_owned) {
                    free(key);
                    neverc_json_free(val);
                    goto err;
                }
                old_owned->parent = NULL;
                neverc_json_free(old);
                obj->u.obj.pairs[ex].value = val;
                object_owned->children.object_pairs[ex].value = val;
                value_owned->parent = obj;
                free(key);
            } else {
                if (json_obj_append(obj, key, key_len, val) < 0 ||
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
            if (neverc_json_object_set_n(obj, key, key_len, val) < 0) {
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

static NCI_JSON_NOINLINE neverc_json_value_t *parse_value(parser_t *p) {
    int c = peek(p);
    if (c < 0) return NULL;
    /* Recursion guard: only arrays/objects re-enter parse_value. Scalars must
     * not consume a nesting slot — otherwise 1000-deep `[0]` / `{"a":null}`
     * (legal under the same cap as empty `[[[]]]`) fail, while marshal of a
     * constructed tree of that depth succeeds. */
    int nested = (c == '[' || c == '{');
    if (nested && ++p->depth > NCI_JSON_MAX_DEPTH) {
        p->depth--;
        return NULL;
    }
    neverc_json_value_t *v;
    switch (c) {
        case 'n': v = parse_null(p); break;
        case 't': case 'f': v = parse_bool(p); break;
        case '"': v = parse_string(p); break;
        case '[': v = parse_array(p); break;
        case '{': v = parse_object(p); break;
        default:  v = parse_number(p); break;
    }
    if (nested) p->depth--;
    return v;
}

/* ---- public API ---- */

neverc_json_value_t *neverc_json_parse(const char *text, size_t len) {
    if (!text && len > 0) return NULL;
    if (text && len >= 3 &&
        (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF) {
        text += 3;
        len -= 3;
    }
    parser_t p = { .src = text, .len = len, .pos = 0, .depth = 0 };
    neverc_json_value_t *v = parse_value(&p);
    if (!v) return NULL;
    skip_ws(&p);
    if (p.pos != p.len) { neverc_json_free(v); return NULL; }
    return v;
}

/* Iterative free: constructors can build trees deeper than NCI_JSON_MAX_DEPTH,
 * and a recursive walk would overflow the C stack on release. */
static void json_free_owned(neverc_json_value_t *root) {
    neverc_json_value_t *cur = root;
    while (cur) {
        json_owned_value_t *owned = json_owned_find(cur);
        if (!owned) return;
        if (owned->owned_type == NEVERC_JSON_ARRAY &&
            owned->private_len > 0) {
            int index = --owned->private_len;
            neverc_json_value_t *child = owned->children.array_values[index];
            owned->children.array_values[index] = NULL;
            if (cur->u.arr.items == owned->public_storage &&
                cur->u.arr.len > index) {
                cur->u.arr.items[index] = NULL;
                cur->u.arr.len = index;
            }
            json_owned_value_t *child_owned = json_owned_find(child);
            if (child_owned && child_owned->parent == cur) {
                cur = child;
                continue;
            }
            continue;
        }
        if (owned->owned_type == NEVERC_JSON_OBJECT &&
            owned->private_len > 0) {
            int index = --owned->private_len;
            json_pair_meta_t *pair = &owned->children.object_pairs[index];
            free(pair->owned_key);
            pair->owned_key = NULL;
            pair->key_view = NULL;
            neverc_json_value_t *child = pair->value;
            pair->value = NULL;
            if (cur->u.obj.pairs == owned->public_storage &&
                cur->u.obj.len > index) {
                cur->u.obj.pairs[index].key = NULL;
                cur->u.obj.pairs[index].value = NULL;
                cur->u.obj.len = index;
            }
            json_owned_value_t *child_owned = json_owned_find(child);
            if (child_owned && child_owned->parent == cur) {
                cur = child;
                continue;
            }
            continue;
        }

        neverc_json_value_t *parent = (cur == root) ? NULL : owned->parent;
        free(owned->owned_string);
        free(owned->public_storage);
        if (owned->owned_type == NEVERC_JSON_ARRAY)
            free(owned->children.array_values);
        else if (owned->owned_type == NEVERC_JSON_OBJECT)
            free(owned->children.object_pairs);
        (void)json_registry_remove(cur);
        free(owned);
        cur = parent;
    }
}

void neverc_json_free(neverc_json_value_t *v) {
    json_owned_value_t *owned = json_owned_find(v);
    if (!owned || owned->parent) return;
    json_free_owned(v);
}

/* ---- marshal ---- */

typedef struct {
    char *buf;
    size_t cap, pos;
    const char *indent;
    int depth;
} marshal_t;

static int mw(marshal_t *m, const char *s, size_t n) {
    if ((!s && n > 0) || m->pos > m->cap || n > m->cap - m->pos)
        return -1;
    if (n > 0) memcpy(m->buf + m->pos, s, n);
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

/* A byte needs escaping iff it is '"', '\\', a control char, or HTML/JS
 * metacharacters. Go encoding/json.Marshal HTML-escapes <, >, & so inlined
 * JSON cannot close a <script> tag or inject markup; U+2028/U+2029 are
 * JavaScript line terminators and are escaped the same way. */
static int json_needs_escape(unsigned char c) {
    return c < 0x20 || c == '"' || c == '\\' ||
           c == '<' || c == '>' || c == '&';
}

static int json_line_separator(const char *p, const char *end) {
    /* Subtraction form: `p + 2 < end` wraps when p is near the address-space
     * end and would treat an out-of-range 3-byte read as in-bounds. */
    return p < end && (size_t)(end - p) >= 3U &&
           (unsigned char)p[0] == 0xE2 &&
           (unsigned char)p[1] == 0x80 &&
           ((unsigned char)p[2] == 0xA8 || (unsigned char)p[2] == 0xA9);
}

static int marshal_string(marshal_t *m, const char *s, size_t len) {
    if (!s || !valid_utf8(s, len)) return -1;
    if (mw_char(m, '"') < 0) return -1;
    const char *p = s;
    const char *end = s + len;
    while (p < end) {
        const char *run = p;
        while (p < end && !json_needs_escape((unsigned char)*p) &&
               !json_line_separator(p, end))
            p++;
        if (p > run && mw(m, run, (size_t)(p - run)) < 0) return -1;
        if (p == end) break;
        if (json_line_separator(p, end)) {
            if (mw(m, (unsigned char)p[2] == 0xA8 ? "\\u2028" : "\\u2029",
                   6) < 0)
                return -1;
            p += 3;
            continue;
        }
        unsigned char c = (unsigned char)*p++;
        switch (c) {
            case '"':  if (mw(m, "\\\"", 2) < 0) return -1; break;
            case '\\': if (mw(m, "\\\\", 2) < 0) return -1; break;
            case '<':  if (mw(m, "\\u003c", 6) < 0) return -1; break;
            case '>':  if (mw(m, "\\u003e", 6) < 0) return -1; break;
            case '&':  if (mw(m, "\\u0026", 6) < 0) return -1; break;
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

/* Keep the float formatter out of the recursive marshal frame.  Under full
 * LTO its conversion scratch space otherwise gets inlined into marshal_value,
 * making every nesting level consume about 1 KiB on Windows' 1 MiB stack. */
static NCI_JSON_NOINLINE int marshal_number(marshal_t *m, double val) {
    if (!isfinite(val)) return -1;

    /* Go encoding/json float64Encoder: ES6 cutoffs, not strconv 'g'.
     * 'f' unless 0 < |x| < 1e-6 or |x| >= 1e21; then strip e-0N padding. */
    char tmp[40];
    double absv = fabs(val);
    char fmt = 'f';
    if (absv != 0.0 && (absv < 1e-6 || absv >= 1e21))
        fmt = 'e';
    int n = neverc_strconv_format_float(val, fmt, -1, tmp, sizeof tmp);
    if (n < 0) return -1;
    if (fmt == 'e' && n >= 4 && tmp[n - 4] == 'e' && tmp[n - 3] == '-' &&
        tmp[n - 2] == '0') {
        tmp[n - 2] = tmp[n - 1];
        tmp[n - 1] = '\0';
        n--;
    }
    return mw(m, tmp, (size_t)n);
}

static NCI_JSON_NOINLINE int marshal_value(
    marshal_t *m, const neverc_json_value_t *v) {
    if (!v) return -1;
    json_owned_value_t *owned = json_owned_find(v);
    if (owned && v->type != owned->owned_type) return -1;
    switch (v->type) {
        case NEVERC_JSON_NULL:
            return mw(m, "null", 4);
        case NEVERC_JSON_BOOL:
            return v->u.bool_val ? mw(m, "true", 4) : mw(m, "false", 5);
        case NEVERC_JSON_NUMBER:
            return marshal_number(m, v->u.num_val);
        case NEVERC_JSON_STRING: {
            if (owned && v->u.str_val != owned->string_view) return -1;
            size_t length = owned ? owned->string_len
                                  : (v->u.str_val ? strlen(v->u.str_val) : 0U);
            return marshal_string(m, v->u.str_val, length);
        }
        case NEVERC_JSON_ARRAY: {
            if (owned && !json_array_shape_valid(v, owned)) return -1;
            if (v->u.arr.len < 0 || v->u.arr.cap < 0 ||
                v->u.arr.len > v->u.arr.cap ||
                (v->u.arr.len > 0 && !v->u.arr.items) ||
                m->depth >= NCI_JSON_MAX_DEPTH)
                return -1;
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
            if (owned && !json_object_shape_valid(v, owned)) return -1;
            if (v->u.obj.len < 0 || v->u.obj.cap < 0 ||
                v->u.obj.len > v->u.obj.cap ||
                (v->u.obj.len > 0 && !v->u.obj.pairs) ||
                m->depth >= NCI_JSON_MAX_DEPTH)
                return -1;
            if (mw_char(m, '{') < 0) return -1;
            m->depth++;
            for (int i = 0; i < v->u.obj.len; i++) {
                if (i > 0) { if (mw_char(m, ',') < 0) return -1; }
                if (m->indent) { if (mw_indent(m) < 0) return -1; }
                const neverc_json_pair_t *pair = &v->u.obj.pairs[i];
                size_t key_len = json_pair_key_length(v, i, pair);
                if (marshal_string(m, pair->key, key_len) < 0) return -1;
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
    if (!v || (!dst && dst_len > 0)) return -1;
    if (indent) {
        for (const unsigned char *p = (const unsigned char *)indent;
             *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
                return -1;
        }
    }
    marshal_t m = { .buf = dst, .cap = dst_len, .pos = 0,
                    .indent = indent, .depth = 0 };
    if (marshal_value(&m, v) < 0) return -1;
    return m.pos <= (size_t)INT_MAX ? (int)m.pos : -1;
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
    if (!v || v->type != NEVERC_JSON_STRING) return "";
    json_owned_value_t *owned = json_owned_find(v);
    if (owned && (owned->owned_type != NEVERC_JSON_STRING ||
                  v->u.str_val != owned->string_view))
        return "";
    return v->u.str_val ? v->u.str_val : "";
}

size_t neverc_json_string_len(const neverc_json_value_t *v) {
    if (!v || v->type != NEVERC_JSON_STRING || !v->u.str_val) return 0;
    json_owned_value_t *owned = json_owned_find(v);
    if (owned) {
        if (owned->owned_type != NEVERC_JSON_STRING ||
            v->u.str_val != owned->string_view)
            return 0;
        return owned->string_len;
    }
    return strlen(v->u.str_val);
}

int neverc_json_array_len(const neverc_json_value_t *v) {
    if (!v || v->type != NEVERC_JSON_ARRAY) return 0;
    json_owned_value_t *owned = json_owned_find(v);
    if (owned && !json_array_shape_valid(v, owned)) return 0;
    return v->u.arr.len >= 0 ? v->u.arr.len : 0;
}

neverc_json_value_t *neverc_json_array_get(const neverc_json_value_t *v, int idx) {
    if (!v || v->type != NEVERC_JSON_ARRAY || idx < 0 ||
        v->u.arr.len < 0 || v->u.arr.cap < 0 ||
        v->u.arr.len > v->u.arr.cap || idx >= v->u.arr.len ||
        !v->u.arr.items)
        return NULL;
    json_owned_value_t *owned = json_owned_find(v);
    if (owned && !json_array_shape_valid(v, owned)) return NULL;
    return v->u.arr.items[idx];
}

int neverc_json_object_len(const neverc_json_value_t *v) {
    if (!v || v->type != NEVERC_JSON_OBJECT) return 0;
    json_owned_value_t *owned = json_owned_find(v);
    if (owned && !json_object_shape_valid(v, owned)) return 0;
    return v->u.obj.len >= 0 ? v->u.obj.len : 0;
}

neverc_json_value_t *neverc_json_object_get_n(
    const neverc_json_value_t *v, const char *key, size_t key_len) {
    if (!v || v->type != NEVERC_JSON_OBJECT ||
        (!key && key_len > 0) || v->u.obj.len < 0 || v->u.obj.cap < 0 ||
        v->u.obj.len > v->u.obj.cap ||
        (v->u.obj.len > 0 && !v->u.obj.pairs)) return NULL;
    json_owned_value_t *owned = json_owned_find(v);
    if (owned && !json_object_shape_valid(v, owned)) return NULL;
    for (int i = 0; i < v->u.obj.len; i++) {
        const neverc_json_pair_t *pair = &v->u.obj.pairs[i];
        size_t pair_len = json_pair_key_length(v, i, pair);
        if (pair->key && pair_len == key_len &&
            (key_len == 0 || memcmp(pair->key, key, key_len) == 0))
            return pair->value;
    }
    return NULL;
}

neverc_json_value_t *neverc_json_object_get(
    const neverc_json_value_t *v, const char *key) {
    return key ? neverc_json_object_get_n(v, key, strlen(key)) : NULL;
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

neverc_json_value_t *neverc_json_new_string_n(const char *s, size_t len) {
    if ((!s && len > 0) || len == SIZE_MAX || !valid_utf8(s, len)) return NULL;
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_STRING);
    if (v) {
        v->u.str_val = dup_str(s, len);
        if (!v->u.str_val) { json_free_single_allocation(v); return NULL; }
        json_owned_value_t *owned = json_owned_find(v);
        owned->owned_string = v->u.str_val;
        owned->string_view = v->u.str_val;
        owned->string_len = len;
    }
    return v;
}

neverc_json_value_t *neverc_json_new_string(const char *s) {
    return s ? neverc_json_new_string_n(s, strlen(s)) : NULL;
}

neverc_json_value_t *neverc_json_new_array(void) {
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_ARRAY);
    if (v) {
        json_owned_value_t *owned = json_owned_find(v);
        if (!owned || json_array_grow(v, owned, 8) != 0) {
            json_free_single_allocation(v);
            return NULL;
        }
    }
    return v;
}

neverc_json_value_t *neverc_json_new_object(void) {
    neverc_json_value_t *v = alloc_val(NEVERC_JSON_OBJECT);
    if (v) {
        json_owned_value_t *owned = json_owned_find(v);
        if (!owned || json_object_grow(v, owned, 8) != 0) {
            json_free_single_allocation(v);
            return NULL;
        }
    }
    return v;
}

static int json_would_create_cycle(const neverc_json_value_t *container,
                                   const neverc_json_value_t *child) {
    const neverc_json_value_t *p = container;
    while (p) {
        if (p == child) return 1;
        json_owned_value_t *owned = json_owned_find(p);
        if (!owned) return 1;
        p = owned->parent;
    }
    return 0;
}

int neverc_json_array_append(neverc_json_value_t *arr, neverc_json_value_t *val) {
    json_owned_value_t *array_owned = json_owned_find(arr);
    json_owned_value_t *value_owned = json_owned_find(val);
    if (!json_array_storage_valid(arr, array_owned) || !value_owned ||
        value_owned->parent || json_would_create_cycle(arr, val))
        return -1;
    if (array_owned->private_len >= array_owned->private_cap) {
        if (array_owned->private_cap > INT_MAX / 2) return -1;
        int capacity = array_owned->private_cap == 0
            ? 8 : array_owned->private_cap * 2;
        if (json_array_grow(arr, array_owned, capacity) != 0) return -1;
    }
    int index = array_owned->private_len++;
    arr->u.arr.items[index] = val;
    arr->u.arr.len = array_owned->private_len;
    array_owned->children.array_values[index] = val;
    value_owned->parent = arr;
    return 0;
}

int neverc_json_object_set_n(neverc_json_value_t *obj,
                             const char *key, size_t key_len,
                             neverc_json_value_t *val) {
    json_owned_value_t *object_owned = json_owned_find(obj);
    json_owned_value_t *value_owned = json_owned_find(val);
    if (!json_object_storage_valid(obj, object_owned) ||
        (!key && key_len > 0) || key_len == SIZE_MAX || !value_owned ||
        !valid_utf8(key, key_len))
        return -1;
    /* overwrite existing key */
    for (int i = 0; i < object_owned->private_len; i++) {
        neverc_json_pair_t *pair = &obj->u.obj.pairs[i];
        json_pair_meta_t *pair_meta =
            &object_owned->children.object_pairs[i];
        if (pair->key != pair_meta->key_view ||
            pair->value != pair_meta->value)
            return -1;
        size_t pair_len = pair_meta->key_len;
        if (pair->key && pair_len == key_len &&
            (key_len == 0 || memcmp(pair->key, key, key_len) == 0)) {
            if (pair->value == val) return 0;
            if (value_owned->parent || json_would_create_cycle(obj, val))
                return -1;
            if (pair->value) {
                json_owned_value_t *old_owned = json_owned_find(pair->value);
                if (!old_owned || old_owned->parent != obj) return -1;
                old_owned->parent = NULL;
                neverc_json_free(pair->value);
            }
            pair->value = val;
            pair_meta->value = val;
            value_owned->parent = obj;
            return 0;
        }
    }
    if (value_owned->parent || json_would_create_cycle(obj, val)) return -1;
    if (object_owned->private_len >= object_owned->private_cap) {
        if (object_owned->private_cap > INT_MAX / 2) return -1;
        int capacity = object_owned->private_cap == 0
            ? 8 : object_owned->private_cap * 2;
        if (json_object_grow(obj, object_owned, capacity) != 0) return -1;
    }
    char *owned_key = dup_str(key, key_len);
    if (!owned_key) return -1;
    int index = object_owned->private_len++;
    obj->u.obj.pairs[index].key = owned_key;
    obj->u.obj.pairs[index].value = val;
    object_owned->children.object_pairs[index].owned_key = owned_key;
    object_owned->children.object_pairs[index].key_view = owned_key;
    object_owned->children.object_pairs[index].key_len = key_len;
    object_owned->children.object_pairs[index].value = val;
    obj->u.obj.len = object_owned->private_len;
    value_owned->parent = obj;
    return 0;
}

int neverc_json_object_set(neverc_json_value_t *obj, const char *key,
                           neverc_json_value_t *val) {
    return key ? neverc_json_object_set_n(obj, key, strlen(key), val) : -1;
}

int neverc_json_valid(const char *text, size_t len) {
    neverc_json_value_t *v = neverc_json_parse(text, len);
    if (!v) return 0;
    neverc_json_free(v);
    return 1;
}

#undef NCI_JSON_NOINLINE
