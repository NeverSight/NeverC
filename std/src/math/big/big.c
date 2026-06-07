#include "neverc/std/math/big.h"
#include <stdlib.h>
#include <string.h>

static void ensure_cap(neverc_bigint_t *z, size_t need) {
    if (need <= z->cap) return;
    size_t newcap = z->cap ? z->cap * 2 : 4;
    while (newcap < need) newcap *= 2;
    uint32_t *nd = (uint32_t *)realloc(z->digits, newcap * sizeof(uint32_t));
    if (!nd) return;
    memset(nd + z->cap, 0, (newcap - z->cap) * sizeof(uint32_t));
    z->digits = nd;
    z->cap = newcap;
}

static void trim(neverc_bigint_t *z) {
    while (z->len > 0 && z->digits[z->len - 1] == 0)
        z->len--;
    if (z->len == 0) z->neg = 0;
}

static int abs_cmp(const neverc_bigint_t *x, const neverc_bigint_t *y) {
    if (x->len != y->len)
        return x->len < y->len ? -1 : 1;
    for (size_t i = x->len; i > 0; i--) {
        if (x->digits[i-1] != y->digits[i-1])
            return x->digits[i-1] < y->digits[i-1] ? -1 : 1;
    }
    return 0;
}

static void abs_add(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len > y->len ? x->len : y->len;
    ensure_cap(z, m + 1);
    uint64_t carry = 0;
    for (size_t i = 0; i < m || carry; i++) {
        uint64_t sum = carry;
        if (i < x->len) sum += x->digits[i];
        if (i < y->len) sum += y->digits[i];
        if (i >= z->cap) ensure_cap(z, i + 1);
        z->digits[i] = (uint32_t)(sum & 0xFFFFFFFFULL);
        carry = sum >> 32;
        if (i >= z->len) z->len = i + 1;
    }
    if (carry && z->len <= m) z->len = m + 1;
    trim(z);
}

static void abs_sub(neverc_bigint_t *z, const neverc_bigint_t *a, const neverc_bigint_t *b) {
    ensure_cap(z, a->len);
    int64_t borrow = 0;
    for (size_t i = 0; i < a->len; i++) {
        int64_t diff = (int64_t)a->digits[i] - borrow;
        if (i < b->len) diff -= (int64_t)b->digits[i];
        if (diff < 0) { diff += (int64_t)1 << 32; borrow = 1; }
        else { borrow = 0; }
        z->digits[i] = (uint32_t)diff;
    }
    z->len = a->len;
    trim(z);
}

void neverc_bigint_init(neverc_bigint_t *z) {
    z->digits = NULL;
    z->len = 0;
    z->cap = 0;
    z->neg = 0;
}

void neverc_bigint_free(neverc_bigint_t *z) {
    if (z->digits) { free(z->digits); z->digits = NULL; }
    z->len = z->cap = 0;
    z->neg = 0;
}

void neverc_bigint_set_int64(neverc_bigint_t *z, int64_t x) {
    z->neg = (x < 0);
    uint64_t v = (uint64_t)(x < 0 ? -x : x);
    if (v == 0) { z->len = 0; return; }
    ensure_cap(z, 2);
    z->digits[0] = (uint32_t)(v & 0xFFFFFFFFULL);
    z->digits[1] = (uint32_t)(v >> 32);
    z->len = z->digits[1] ? 2 : 1;
}

void neverc_bigint_set_uint64(neverc_bigint_t *z, uint64_t x) {
    z->neg = 0;
    if (x == 0) { z->len = 0; return; }
    ensure_cap(z, 2);
    z->digits[0] = (uint32_t)(x & 0xFFFFFFFFULL);
    z->digits[1] = (uint32_t)(x >> 32);
    z->len = z->digits[1] ? 2 : 1;
}

void neverc_bigint_set(neverc_bigint_t *z, const neverc_bigint_t *x) {
    if (z == x) return;
    ensure_cap(z, x->len);
    memcpy(z->digits, x->digits, x->len * sizeof(uint32_t));
    z->len = x->len;
    z->neg = x->neg;
}

int neverc_bigint_set_string(neverc_bigint_t *z, const char *s, int base) {
    z->len = 0; z->neg = 0;
    if (!s || !*s) return -1;
    const char *p = s;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    if (base == 0) {
        base = 10;
        if (*p == '0') {
            p++;
            if (*p == 'x' || *p == 'X') { base = 16; p++; }
            else if (*p == 'b' || *p == 'B') { base = 2; p++; }
            else if (*p == 'o' || *p == 'O') { base = 8; p++; }
            else { base = 8; }
        }
    }
    if (base < 2 || base > 36) return -1;

    neverc_bigint_t b, digit;
    neverc_bigint_init(&b);
    neverc_bigint_init(&digit);
    neverc_bigint_set_int64(&b, base);

    ensure_cap(z, 1);
    z->len = 0;

    for (; *p; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else if (*p == '_') continue;
        else { neverc_bigint_free(&b); neverc_bigint_free(&digit); return -1; }
        if (d >= base) { neverc_bigint_free(&b); neverc_bigint_free(&digit); return -1; }

        neverc_bigint_mul(z, z, &b);
        neverc_bigint_set_int64(&digit, d);
        neverc_bigint_add(z, z, &digit);
    }

    z->neg = neg && z->len > 0;
    neverc_bigint_free(&b);
    neverc_bigint_free(&digit);
    return 0;
}

int64_t neverc_bigint_int64(const neverc_bigint_t *x) {
    uint64_t v = 0;
    if (x->len >= 1) v = x->digits[0];
    if (x->len >= 2) v |= (uint64_t)x->digits[1] << 32;
    return x->neg ? -(int64_t)v : (int64_t)v;
}

uint64_t neverc_bigint_uint64(const neverc_bigint_t *x) {
    uint64_t v = 0;
    if (x->len >= 1) v = x->digits[0];
    if (x->len >= 2) v |= (uint64_t)x->digits[1] << 32;
    return v;
}

int neverc_bigint_sign(const neverc_bigint_t *x) {
    if (x->len == 0) return 0;
    return x->neg ? -1 : 1;
}

int neverc_bigint_cmp(const neverc_bigint_t *x, const neverc_bigint_t *y) {
    if (x->neg != y->neg) {
        if (x->len == 0 && y->len == 0) return 0;
        return x->neg ? -1 : 1;
    }
    int c = abs_cmp(x, y);
    return x->neg ? -c : c;
}

int neverc_bigint_is_zero(const neverc_bigint_t *x) {
    return x->len == 0;
}

int neverc_bigint_bit_len(const neverc_bigint_t *x) {
    if (x->len == 0) return 0;
    uint32_t top = x->digits[x->len - 1];
    int bits = (int)(x->len - 1) * 32;
    while (top) { bits++; top >>= 1; }
    return bits;
}

void neverc_bigint_add(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t tx, ty;
    neverc_bigint_init(&tx); neverc_bigint_init(&ty);
    if (z == x) { neverc_bigint_set(&tx, x); x = &tx; }
    if (z == y) { neverc_bigint_set(&ty, y); y = &ty; }

    if (x->neg == y->neg) {
        abs_add(z, x, y);
        z->neg = x->neg;
    } else {
        int c = abs_cmp(x, y);
        if (c == 0) { z->len = 0; z->neg = 0; }
        else if (c > 0) { abs_sub(z, x, y); z->neg = x->neg; }
        else { abs_sub(z, y, x); z->neg = y->neg; }
    }
    trim(z);
    neverc_bigint_free(&tx); neverc_bigint_free(&ty);
}

void neverc_bigint_sub(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t ny;
    neverc_bigint_init(&ny);
    neverc_bigint_set(&ny, y);
    ny.neg = !ny.neg;
    if (ny.len == 0) ny.neg = 0;
    neverc_bigint_add(z, x, &ny);
    neverc_bigint_free(&ny);
}

void neverc_bigint_mul(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    if (x->len == 0 || y->len == 0) {
        z->len = 0; z->neg = 0; return;
    }

    neverc_bigint_t result;
    neverc_bigint_init(&result);
    size_t rlen = x->len + y->len;
    ensure_cap(&result, rlen);
    memset(result.digits, 0, rlen * sizeof(uint32_t));
    result.len = rlen;

    for (size_t i = 0; i < x->len; i++) {
        uint64_t carry = 0;
        for (size_t j = 0; j < y->len; j++) {
            uint64_t prod = (uint64_t)x->digits[i] * y->digits[j]
                          + result.digits[i + j] + carry;
            result.digits[i + j] = (uint32_t)(prod & 0xFFFFFFFFULL);
            carry = prod >> 32;
        }
        if (carry) result.digits[i + y->len] += (uint32_t)carry;
    }

    result.neg = (x->neg != y->neg);
    trim(&result);
    neverc_bigint_free(z);
    *z = result;
}

void neverc_bigint_div(neverc_bigint_t *q, neverc_bigint_t *r,
                       const neverc_bigint_t *x, const neverc_bigint_t *y) {
    if (y->len == 0) return;

    int c = abs_cmp(x, y);
    if (c < 0) {
        if (r) neverc_bigint_set(r, x);
        if (q) { q->len = 0; q->neg = 0; }
        return;
    }
    if (c == 0) {
        if (q) neverc_bigint_set_int64(q, 1);
        if (r) { r->len = 0; r->neg = 0; }
        if (q) q->neg = (x->neg != y->neg);
        return;
    }

    if (y->len == 1) {
        uint32_t d = y->digits[0];
        neverc_bigint_t quot;
        neverc_bigint_init(&quot);
        ensure_cap(&quot, x->len);
        quot.len = x->len;
        uint64_t rem = 0;
        for (size_t i = x->len; i > 0; i--) {
            uint64_t cur = (rem << 32) | x->digits[i-1];
            quot.digits[i-1] = (uint32_t)(cur / d);
            rem = cur % d;
        }
        trim(&quot);
        quot.neg = (x->neg != y->neg);
        if (q) { neverc_bigint_free(q); *q = quot; }
        else { neverc_bigint_free(&quot); }
        if (r) {
            neverc_bigint_set_uint64(r, rem);
            r->neg = x->neg && rem > 0;
        }
        return;
    }

    neverc_bigint_t rem, divisor, tmp;
    neverc_bigint_init(&rem);
    neverc_bigint_init(&divisor);
    neverc_bigint_init(&tmp);
    neverc_bigint_set(&rem, x);
    rem.neg = 0;
    neverc_bigint_set(&divisor, y);
    divisor.neg = 0;

    neverc_bigint_t quot;
    neverc_bigint_init(&quot);

    int bit_diff = neverc_bigint_bit_len(&rem) - neverc_bigint_bit_len(&divisor);
    if (bit_diff < 0) bit_diff = 0;

    neverc_bigint_t shifted;
    neverc_bigint_init(&shifted);

    for (int i = bit_diff; i >= 0; i--) {
        neverc_bigint_lsh(&shifted, &divisor, (unsigned)i);
        if (abs_cmp(&rem, &shifted) >= 0) {
            abs_sub(&rem, &rem, &shifted);
            trim(&rem);

            size_t word = (unsigned)i / 32;
            unsigned bit = (unsigned)i % 32;
            ensure_cap(&quot, word + 1);
            if (quot.len <= word) {
                memset(quot.digits + quot.len, 0, (word + 1 - quot.len) * sizeof(uint32_t));
                quot.len = word + 1;
            }
            quot.digits[word] |= (1U << bit);
        }
    }

    trim(&quot);
    quot.neg = (x->neg != y->neg) && quot.len > 0;
    rem.neg = x->neg && rem.len > 0;

    if (q) { neverc_bigint_free(q); *q = quot; }
    else { neverc_bigint_free(&quot); }
    if (r) { neverc_bigint_free(r); *r = rem; }
    else { neverc_bigint_free(&rem); }

    neverc_bigint_free(&divisor);
    neverc_bigint_free(&tmp);
    neverc_bigint_free(&shifted);
}

void neverc_bigint_mod(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *m) {
    neverc_bigint_div(NULL, z, x, m);
    if (z->neg && z->len > 0) {
        neverc_bigint_add(z, z, m);
    }
}

void neverc_bigint_neg(neverc_bigint_t *z, const neverc_bigint_t *x) {
    neverc_bigint_set(z, x);
    if (z->len > 0) z->neg = !z->neg;
}

void neverc_bigint_abs(neverc_bigint_t *z, const neverc_bigint_t *x) {
    neverc_bigint_set(z, x);
    z->neg = 0;
}

void neverc_bigint_lsh(neverc_bigint_t *z, const neverc_bigint_t *x, unsigned n) {
    if (x->len == 0) { z->len = 0; z->neg = 0; return; }
    unsigned words = n / 32;
    unsigned bits = n % 32;
    size_t newlen = x->len + words + 1;

    neverc_bigint_t tmp;
    neverc_bigint_init(&tmp);
    ensure_cap(&tmp, newlen);
    memset(tmp.digits, 0, newlen * sizeof(uint32_t));
    tmp.len = newlen;
    tmp.neg = x->neg;

    for (size_t i = 0; i < x->len; i++) {
        uint64_t v = (uint64_t)x->digits[i] << bits;
        tmp.digits[i + words] |= (uint32_t)(v & 0xFFFFFFFFULL);
        if (i + words + 1 < newlen)
            tmp.digits[i + words + 1] |= (uint32_t)(v >> 32);
    }

    trim(&tmp);
    neverc_bigint_free(z);
    *z = tmp;
}

void neverc_bigint_rsh(neverc_bigint_t *z, const neverc_bigint_t *x, unsigned n) {
    unsigned words = n / 32;
    unsigned bits = n % 32;

    if (words >= x->len) { z->len = 0; z->neg = 0; return; }

    size_t newlen = x->len - words;
    neverc_bigint_t tmp;
    neverc_bigint_init(&tmp);
    ensure_cap(&tmp, newlen);
    tmp.len = newlen;
    tmp.neg = x->neg;

    for (size_t i = 0; i < newlen; i++) {
        tmp.digits[i] = x->digits[i + words] >> bits;
        if (bits > 0 && i + words + 1 < x->len)
            tmp.digits[i] |= x->digits[i + words + 1] << (32 - bits);
    }

    trim(&tmp);
    neverc_bigint_free(z);
    *z = tmp;
}

void neverc_bigint_and(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len < y->len ? x->len : y->len;
    ensure_cap(z, m);
    for (size_t i = 0; i < m; i++)
        z->digits[i] = x->digits[i] & y->digits[i];
    z->len = m;
    z->neg = 0;
    trim(z);
}

void neverc_bigint_or(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len > y->len ? x->len : y->len;
    ensure_cap(z, m);
    for (size_t i = 0; i < m; i++) {
        uint32_t a = i < x->len ? x->digits[i] : 0;
        uint32_t b = i < y->len ? y->digits[i] : 0;
        z->digits[i] = a | b;
    }
    z->len = m;
    z->neg = 0;
    trim(z);
}

void neverc_bigint_xor(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len > y->len ? x->len : y->len;
    ensure_cap(z, m);
    for (size_t i = 0; i < m; i++) {
        uint32_t a = i < x->len ? x->digits[i] : 0;
        uint32_t b = i < y->len ? y->digits[i] : 0;
        z->digits[i] = a ^ b;
    }
    z->len = m;
    z->neg = 0;
    trim(z);
}

int neverc_bigint_bit(const neverc_bigint_t *x, unsigned i) {
    unsigned word = i / 32;
    unsigned bit = i % 32;
    if ((size_t)word >= x->len) return 0;
    return (x->digits[word] >> bit) & 1;
}

void neverc_bigint_exp(neverc_bigint_t *z, const neverc_bigint_t *base,
                       const neverc_bigint_t *exp, const neverc_bigint_t *m) {
    neverc_bigint_t result, b, e;
    neverc_bigint_init(&result);
    neverc_bigint_init(&b);
    neverc_bigint_init(&e);

    neverc_bigint_set_int64(&result, 1);
    neverc_bigint_set(&b, base);
    b.neg = 0;
    neverc_bigint_set(&e, exp);

    if (m && m->len > 0)
        neverc_bigint_mod(&b, &b, m);

    int bits = neverc_bigint_bit_len(&e);
    for (int i = 0; i < bits; i++) {
        if (neverc_bigint_bit(&e, (unsigned)i)) {
            neverc_bigint_mul(&result, &result, &b);
            if (m && m->len > 0)
                neverc_bigint_mod(&result, &result, m);
        }
        neverc_bigint_mul(&b, &b, &b);
        if (m && m->len > 0)
            neverc_bigint_mod(&b, &b, m);
    }

    neverc_bigint_free(z);
    *z = result;
    neverc_bigint_free(&b);
    neverc_bigint_free(&e);
}

void neverc_bigint_gcd(neverc_bigint_t *z, const neverc_bigint_t *x,
                       const neverc_bigint_t *y) {
    neverc_bigint_t a, b, tmp;
    neverc_bigint_init(&a);
    neverc_bigint_init(&b);
    neverc_bigint_init(&tmp);

    neverc_bigint_abs(&a, x);
    neverc_bigint_abs(&b, y);

    while (b.len > 0) {
        neverc_bigint_mod(&tmp, &a, &b);
        neverc_bigint_set(&a, &b);
        neverc_bigint_set(&b, &tmp);
    }

    neverc_bigint_set(z, &a);
    neverc_bigint_free(&a);
    neverc_bigint_free(&b);
    neverc_bigint_free(&tmp);
}

int neverc_bigint_string(const neverc_bigint_t *x, int base, char *buf, size_t cap) {
    if (base < 2 || base > 36 || !buf || cap == 0) return -1;
    if (x->len == 0) {
        if (cap < 2) return -1;
        buf[0] = '0'; buf[1] = '\0';
        return 1;
    }

    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char tmp[4096];
    int pos = 0;

    neverc_bigint_t v, rem;
    neverc_bigint_init(&v);
    neverc_bigint_init(&rem);
    neverc_bigint_abs(&v, x);

    neverc_bigint_t bval;
    neverc_bigint_init(&bval);
    neverc_bigint_set_int64(&bval, base);

    while (v.len > 0 && pos < (int)sizeof(tmp) - 1) {
        neverc_bigint_div(&v, &rem, &v, &bval);
        int d = (rem.len > 0) ? (int)rem.digits[0] : 0;
        tmp[pos++] = digits[d];
    }

    size_t needed = (size_t)pos + (x->neg ? 1 : 0) + 1;
    if (needed > cap) {
        neverc_bigint_free(&v);
        neverc_bigint_free(&rem);
        neverc_bigint_free(&bval);
        return -1;
    }

    int out = 0;
    if (x->neg) buf[out++] = '-';
    for (int i = pos - 1; i >= 0; i--)
        buf[out++] = tmp[i];
    buf[out] = '\0';

    neverc_bigint_free(&v);
    neverc_bigint_free(&rem);
    neverc_bigint_free(&bval);
    return out;
}
