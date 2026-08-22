#include "neverc/std/math/big.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* Set while a public operation must fail closed on OOM (SetString). Each
 * thread needs its own tracker: TLS certificate validation can parse keys on
 * concurrent TCP and QUIC workers. Scratch fallbacks (Karatsuba→schoolbook)
 * do not use ensure_cap, so they cannot false-trigger this. */
static _Thread_local int *bigint_oom;

static void note_oom(void) {
    if (bigint_oom) *bigint_oom = 1;
}

static int ensure_cap(neverc_bigint_t *z, size_t need) {
    if (!z) return 0;
    if (need > SIZE_MAX / sizeof(uint32_t)) { note_oom(); return 0; }
    if (need <= z->cap) return 1;
    size_t maxcap = SIZE_MAX / sizeof(uint32_t);
    size_t newcap = z->cap ? z->cap * 2 : 4;
    if (z->cap > maxcap / 2) newcap = need;
    while (newcap < need) {
        if (newcap > maxcap / 2) { newcap = need; break; }
        newcap *= 2;
    }
    uint32_t *nd = (uint32_t *)realloc(z->digits, newcap * sizeof(uint32_t));
    if (!nd) { note_oom(); return 0; }
    memset(nd + z->cap, 0, (newcap - z->cap) * sizeof(uint32_t));
    z->digits = nd;
    z->cap = newcap;
    return 1;
}

static void trim(neverc_bigint_t *z) {
    while (z->len > 0 && z->digits[z->len - 1] == 0)
        z->len--;
    if (z->len == 0) z->neg = 0;
}

/* Borrowed view with leading zero words dropped. Untrimmed zeros used to
 * hit word / 0 (UB) or Knuth D's `while (!(t & 0x80000000)) t <<= 1`
 * spinning forever when the top limb was 0. */
static void mag_view(neverc_bigint_t *v, const neverc_bigint_t *x) {
    *v = *x;
    if (!v->digits) {
        v->len = 0;
        v->neg = 0;
        return;
    }
    while (v->len > 0 && v->digits[v->len - 1] == 0)
        v->len--;
    if (v->len == 0) v->neg = 0;
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

static int abs_add(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len > y->len ? x->len : y->len;
    if (m == SIZE_MAX || !ensure_cap(z, m + 1)) return 0;
    uint64_t carry = 0;
    size_t i = 0;
    for (; i < m; i++) {
        uint64_t sum = carry;
        if (i < x->len) sum += x->digits[i];
        if (i < y->len) sum += y->digits[i];
        z->digits[i] = (uint32_t)sum;
        carry = sum >> 32;
    }
    if (carry) z->digits[i++] = (uint32_t)carry;
    /* Set the length explicitly (carry adds at most one word past m): a reused
     * destination holding a longer stale value must shrink, not keep old high
     * words that trim would then mistake for significant digits. */
    z->len = i;
    trim(z);
    return 1;
}

static int abs_sub(neverc_bigint_t *z, const neverc_bigint_t *a, const neverc_bigint_t *b) {
    if (!ensure_cap(z, a->len)) return 0;
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
    return 1;
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
    /* Negate in unsigned space: -x on INT64_MIN is signed-overflow UB. */
    uint64_t v = (x < 0) ? -(uint64_t)x : (uint64_t)x;
    if (v == 0) { z->len = 0; z->neg = 0; return; }
    if (!ensure_cap(z, 2)) return;
    z->neg = (x < 0);
    z->digits[0] = (uint32_t)(v & 0xFFFFFFFFULL);
    z->digits[1] = (uint32_t)(v >> 32);
    z->len = z->digits[1] ? 2 : 1;
}

void neverc_bigint_set_uint64(neverc_bigint_t *z, uint64_t x) {
    if (x == 0) { z->len = 0; z->neg = 0; return; }
    if (!ensure_cap(z, 2)) return;
    z->neg = 0;
    z->digits[0] = (uint32_t)(x & 0xFFFFFFFFULL);
    z->digits[1] = (uint32_t)(x >> 32);
    z->len = z->digits[1] ? 2 : 1;
}

static int bigint_set_checked(neverc_bigint_t *z, const neverc_bigint_t *x) {
    if (z == x) return 1;
    if (x->len == 0) {
        z->len = 0;
        z->neg = 0;
        return 1;
    }
    if (!ensure_cap(z, x->len)) return 0;
    memcpy(z->digits, x->digits, x->len * sizeof(uint32_t));
    z->len = x->len;
    z->neg = x->neg;
    return 1;
}

void neverc_bigint_set(neverc_bigint_t *z, const neverc_bigint_t *x) {
    (void)bigint_set_checked(z, x);
}

/* Above this many word-chunks, parse with the subquadratic divide-and-conquer
 * combine below instead of the digit-at-a-time fold. Overridable for testing. */
#ifndef NCI_BASECONV_THRESHOLD
#define NCI_BASECONV_THRESHOLD 80
#endif

/*
 * Interpret limb[lo..hi) (most-significant first, each < chunk_base) as a
 * base-chunk_base number: out = sum limb[i] * chunk_base^(hi-1-i).
 *
 * The digit-at-a-time fold below is O(n^2) (n multiprecision mul+add over a
 * value that grows to n words). This splits the limbs so the low half always
 * has a power-of-two count b = 2^j, recurses on each half, and recombines with
 *   out = high * chunk_base^b + low   (chunk_base^b == pw2[j], precomputed).
 * The recombine multiply uses Karatsuba, giving O(M(n) log n) — the same
 * divide-and-conquer base conversion Python/Java/Go use for large integers.
 */
static void nat_chunks_combine(neverc_bigint_t *out, const uint32_t *limb,
                               size_t lo, size_t hi, neverc_bigint_t *pw2) {
    if (hi - lo == 1) { neverc_bigint_set_uint64(out, limb[lo]); return; }

    size_t n = hi - lo, b = 1; int j = 0;
    while (b * 2 < n) { b *= 2; j++; }      /* b = largest power of two < n */
    size_t split = hi - b;                  /* low part [split,hi) has b limbs */

    neverc_bigint_t high, low, t;
    neverc_bigint_init(&high); neverc_bigint_init(&low); neverc_bigint_init(&t);
    nat_chunks_combine(&high, limb, lo, split, pw2);
    nat_chunks_combine(&low, limb, split, hi, pw2);
    neverc_bigint_mul(&t, &high, &pw2[j]);  /* high * chunk_base^b */
    neverc_bigint_add(out, &t, &low);
    neverc_bigint_free(&high); neverc_bigint_free(&low); neverc_bigint_free(&t);
}

int neverc_bigint_set_string(neverc_bigint_t *z, const char *s, int base) {
    /* Validate first: a failed parse must leave z unchanged (Go SetString). */
    if (!s || !*s) return -1;
    const char *p = s;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    else if (*p == '+') { p++; }

    int allow_underscores = (base == 0);
    int zero_is_digit = 0;
    int after_prefix = 0;
    if (base == 0) {
        base = 10;
        if (*p == '0') {
            p++;
            if (*p == 'x' || *p == 'X') { base = 16; p++; after_prefix = 1; }
            else if (*p == 'b' || *p == 'B') { base = 2; p++; after_prefix = 1; }
            else if (*p == 'o' || *p == 'O') { base = 8; p++; after_prefix = 1; }
            else { base = 8; zero_is_digit = 1; after_prefix = 1; }
        }
    }
    if (base < 2 || base > 36) return -1;

    /* Largest power of base that fits in a single word: k source digits fold in
     * with one multiprecision mul+add instead of one per digit. */
    uint32_t chunk_base = 1;
    int k = 0;
    while ((uint64_t)chunk_base * (uint32_t)base <= 0xFFFFFFFFULL) {
        chunk_base *= (uint32_t)base;
        k++;
    }

    /* Validate every character once and count the significant digits so the
     * right strategy (and exact limb count) is known up front. Underscores
     * are accepted only with base 0, and only between digits (or after a
     * 0x/0b/0o prefix) — matching strconv and Go big.Int.SetString. */
    size_t ndigits = 0;
    int prev_us = 0;
    int saw_digit = zero_is_digit;
    for (const char *q = p; *q; q++) {
        if (*q == '_') {
            if (!allow_underscores || prev_us || !(saw_digit || after_prefix))
                return -1;
            prev_us = 1;
            after_prefix = 0;
            continue;
        }
        int d;
        if (*q >= '0' && *q <= '9') d = *q - '0';
        else if (*q >= 'a' && *q <= 'z') d = *q - 'a' + 10;
        else if (*q >= 'A' && *q <= 'Z') d = *q - 'A' + 10;
        else return -1;
        if (d >= base) return -1;
        ndigits++;
        saw_digit = 1;
        prev_us = 0;
        after_prefix = 0;
    }
    if (prev_us) return -1;

    if (ndigits == 0) {
        if (zero_is_digit) { z->len = 0; z->neg = 0; return 0; }
        return -1;
    }

    if (k <= 0) return -1;

    /* Parse into a temp. Zeroing z first, then returning 0 after a failed
     * mul/add, reported success with a clobbered or truncated value. */
    int oom = 0;
    bigint_oom = &oom;

    neverc_bigint_t tmp;
    neverc_bigint_init(&tmp);
    size_t m = (ndigits - 1U) / (size_t)k + 1U; /* word-chunks */

    if (m >= (size_t)NCI_BASECONV_THRESHOLD &&
        m <= SIZE_MAX / sizeof(uint32_t)) {
        uint32_t *limb = (uint32_t *)malloc(m * sizeof(uint32_t));
        if (limb) {
            size_t lead = ndigits % (size_t)k;          /* digits in the top limb */
            if (lead == 0) lead = (size_t)k;
            size_t li = 0, cnt = 0, want = lead;
            uint32_t acc = 0;
            for (const char *q = p; *q; q++) {          /* most-significant first */
                int d;
                if (*q >= '0' && *q <= '9') d = *q - '0';
                else if (*q >= 'a' && *q <= 'z') d = *q - 'a' + 10;
                else if (*q >= 'A' && *q <= 'Z') d = *q - 'A' + 10;
                else continue;                          /* '_' (already validated) */
                acc = acc * (uint32_t)base + (uint32_t)d;
                if (++cnt == want) { limb[li++] = acc; acc = 0; cnt = 0; want = (size_t)k; }
            }

            neverc_bigint_t pw2[64];                    /* pw2[j] = chunk_base^(2^j) */
            int np = 0;
            neverc_bigint_init(&pw2[0]);
            neverc_bigint_set_uint64(&pw2[0], chunk_base);
            np = 1;
            while (!oom && np < 63 && ((size_t)1 << np) < m) {
                neverc_bigint_init(&pw2[np]);
                neverc_bigint_mul(&pw2[np], &pw2[np - 1], &pw2[np - 1]);
                np++;
            }

            if (!oom)
                nat_chunks_combine(&tmp, limb, 0, m, pw2);

            for (int i = 0; i < np; i++) neverc_bigint_free(&pw2[i]);
            free(limb);
            if (!oom)
                goto commit;
            neverc_bigint_free(&tmp);
            bigint_oom = NULL;
            return -1;
        }
        /* malloc failed: fall through to the linear-space simple fold */
    }

    /* Small inputs: digit-at-a-time fold (lower constant factor than D&C). */
    neverc_bigint_t bk, digit;
    neverc_bigint_init(&bk);
    neverc_bigint_init(&digit);
    neverc_bigint_set_uint64(&bk, chunk_base);

    uint32_t acc = 0, pmul = 1;
    int cnt = 0;
    for (; *p && !oom; p++) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else if (*p == '_') continue;
        else {
            neverc_bigint_free(&bk); neverc_bigint_free(&digit);
            neverc_bigint_free(&tmp); bigint_oom = NULL; return -1;
        }
        if (d >= base) {
            neverc_bigint_free(&bk); neverc_bigint_free(&digit);
            neverc_bigint_free(&tmp); bigint_oom = NULL; return -1;
        }

        acc = acc * (uint32_t)base + (uint32_t)d;
        pmul *= (uint32_t)base;
        if (++cnt == k) {
            neverc_bigint_mul(&tmp, &tmp, &bk);
            neverc_bigint_set_uint64(&digit, acc);
            neverc_bigint_add(&tmp, &tmp, &digit);
            acc = 0; pmul = 1; cnt = 0;
        }
    }
    if (!oom && cnt > 0) {               /* fold the final partial chunk */
        neverc_bigint_set_uint64(&digit, pmul);
        neverc_bigint_mul(&tmp, &tmp, &digit);
        neverc_bigint_set_uint64(&digit, acc);
        neverc_bigint_add(&tmp, &tmp, &digit);
    }

    neverc_bigint_free(&bk);
    neverc_bigint_free(&digit);
    if (oom) {
        neverc_bigint_free(&tmp);
        bigint_oom = NULL;
        return -1;
    }

commit:
    bigint_oom = NULL;
    tmp.neg = neg && tmp.len > 0;
    neverc_bigint_free(z);
    *z = tmp;
    return 0;
}

int64_t neverc_bigint_int64(const neverc_bigint_t *x) {
    uint64_t v = 0;
    if (x->len >= 1) v = x->digits[0];
    if (x->len >= 2) v |= (uint64_t)x->digits[1] << 32;
    /* Negate in unsigned space then reinterpret: -(int64_t)v is UB when the
     * magnitude is 2^63 (e.g. a bigint equal to INT64_MIN). */
    return x->neg ? (int64_t)(0ULL - v) : (int64_t)v;
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
    neverc_bigint_t view;
    mag_view(&view, x);
    if (view.len == 0) return 0;
    /* (len-1)*32 as int overflows for a few-hundred-MB integer (UB), then
     * string() under-allocates and writes off the end of tmp. */
    if (view.len - 1 > (size_t)INT_MAX / 32)
        return INT_MAX;
    uint32_t top = view.digits[view.len - 1];
    int bits = (int)(view.len - 1) * 32;
    while (top) {
        if (bits == INT_MAX) return INT_MAX;
        bits++;
        top >>= 1;
    }
    return bits;
}

void neverc_bigint_add(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t tx, ty;
    neverc_bigint_init(&tx); neverc_bigint_init(&ty);
    if (z == x) {
        if (!bigint_set_checked(&tx, x)) goto done;
        x = &tx;
    }
    if (z == y) {
        if (!bigint_set_checked(&ty, y)) goto done;
        y = &ty;
    }

    if (x->neg == y->neg) {
        if (!abs_add(z, x, y)) goto done;
        z->neg = x->neg;
    } else {
        int c = abs_cmp(x, y);
        if (c == 0) { z->len = 0; z->neg = 0; }
        else if (c > 0) {
            if (!abs_sub(z, x, y)) goto done;
            z->neg = x->neg;
        } else {
            if (!abs_sub(z, y, x)) goto done;
            z->neg = y->neg;
        }
    }
    trim(z);
done:
    neverc_bigint_free(&tx); neverc_bigint_free(&ty);
}

void neverc_bigint_sub(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t ny;
    neverc_bigint_init(&ny);
    if (!bigint_set_checked(&ny, y)) return;
    ny.neg = !ny.neg;
    if (ny.len == 0) ny.neg = 0;
    neverc_bigint_add(z, x, &ny);
    neverc_bigint_free(&ny);
}

/* Effective length: drop trailing (high) zero words. */
static size_t nat_efflen(const uint32_t *a, size_t n) {
    while (n > 0 && a[n - 1] == 0) n--;
    return n;
}

/* rd[0..xn+yn) += x*y, schoolbook. rd must be pre-zeroed and large enough. */
static void nat_mul_basic(uint32_t *rd, const uint32_t *x, size_t xn,
                          const uint32_t *y, size_t yn) {
    for (size_t i = 0; i < xn; i++) {
        uint64_t carry = 0, xi = x[i];
        size_t kk = i;
        for (size_t j = 0; j < yn; j++, kk++) {
            uint64_t p = xi * y[j] + rd[kk] + carry;
            rd[kk] = (uint32_t)p;
            carry = p >> 32;
        }
        while (carry) {
            uint64_t s = (uint64_t)rd[kk] + carry;
            rd[kk] = (uint32_t)s; carry = s >> 32; kk++;
        }
    }
}

/* r[0..rn) += a[0..an), rn large enough to absorb any carry. */
static void nat_add_into(uint32_t *r, size_t rn, const uint32_t *a, size_t an) {
    uint64_t c = 0; size_t i = 0;
    for (; i < an; i++) { uint64_t s = (uint64_t)r[i] + a[i] + c; r[i] = (uint32_t)s; c = s >> 32; }
    for (; c && i < rn; i++) { uint64_t s = (uint64_t)r[i] + c; r[i] = (uint32_t)s; c = s >> 32; }
}

/* r[0..rn) -= a[0..an), assumes r >= a (no final borrow). */
static void nat_sub_into(uint32_t *r, size_t rn, const uint32_t *a, size_t an) {
    int64_t b = 0; size_t i = 0;
    for (; i < an; i++) {
        int64_t d = (int64_t)r[i] - (int64_t)a[i] - b;
        if (d < 0) { d += ((int64_t)1 << 32); b = 1; } else b = 0;
        r[i] = (uint32_t)d;
    }
    for (; b && i < rn; i++) {
        int64_t d = (int64_t)r[i] - b;
        if (d < 0) { d += ((int64_t)1 << 32); b = 1; } else b = 0;
        r[i] = (uint32_t)d;
    }
}

/* Karatsuba threshold in words (matches Go math/big). Below it, schoolbook
 * wins because of its lower constant factor. Overridable for testing. */
#ifndef NCI_KARATSUBA_THRESHOLD
#define NCI_KARATSUBA_THRESHOLD 40
#endif

/* Word count at/above which squaring (x*x) switches from symmetric schoolbook
 * to Karatsuba squaring. Higher than the multiply threshold because the basic
 * squaring already halves the word-multiplies, so Karatsuba's recursion only
 * pays off later. Overridable for fuzzing; 4 is the smallest value that forces
 * deep recursion while still terminating (2 or 3 loop forever — the
 * (xl+xh)^2 sub-square stays the same word count, so it never shrinks). */
#ifndef NCI_KARATSUBA_SQR_THRESHOLD
#define NCI_KARATSUBA_SQR_THRESHOLD 80
#endif

/* Word count at/above which a balanced multiply switches from Karatsuba to
 * Toom-Cook-3. Karatsuba splits each operand in 2 and does 3 half-size
 * multiplies (O(n^1.585)); Toom-3 splits in 3 and does 5 third-size multiplies
 * (O(n^1.465)) — the next asymptotic tier (as GMP takes past Karatsuba). It
 * only pays once the operands are large enough to amortize the extra
 * evaluation/interpolation passes, and only when balanced (skewed sizes keep
 * Karatsuba, which already tolerates length skew). Correctness is independent
 * of the value (any >= 3 works); overridable for fuzzing. */
#ifndef NCI_TOOM3_THRESHOLD
#define NCI_TOOM3_THRESHOLD 144
#endif
/* Squaring crossover (measured lower than the multiply one): Toom-3 squaring
 * replaces 3 Karatsuba sub-squares with 5 smaller dedicated squares, so it
 * pulls ahead from ~120 words. Overridable for fuzzing; use a small value such
 * as 7 to force deep recursion (Toom-3 needs three non-degenerate parts, so
 * values below ~5 split into too few limbs to make progress). */
#ifndef NCI_TOOM3_SQR_THRESHOLD
#define NCI_TOOM3_SQR_THRESHOLD 120
#endif

static void nat_toom3(uint32_t *rd, const uint32_t *x, size_t xn,
                      const uint32_t *y, size_t yn, size_t k);
static void nat_toom3_sqr(uint32_t *rd, const uint32_t *x, size_t n, size_t k);

/* rd[0..xn+yn) = x*y. rd must be pre-zeroed. Toom-3 for large balanced
 * operands, Karatsuba above the Karatsuba threshold, schoolbook below. */
static void nat_mul(uint32_t *rd, const uint32_t *x, size_t xn,
                    const uint32_t *y, size_t yn) {
    if (xn < yn) { const uint32_t *t = x; x = y; y = t; size_t s = xn; xn = yn; yn = s; }
    if (yn == 0) return;
    if (yn < NCI_KARATSUBA_THRESHOLD) { nat_mul_basic(rd, x, xn, y, yn); return; }

    if (yn >= (size_t)NCI_TOOM3_THRESHOLD) {     /* balanced Toom-3 (3 parts each) */
        size_t k3 = (xn + 2) / 3;
        if (yn > 2 * k3) { nat_toom3(rd, x, xn, y, yn, k3); return; }
    }

    size_t k = yn / 2;                       /* split point, 1 <= k < yn <= xn */
    const uint32_t *xl = x, *xh = x + k; size_t xln = k, xhn = xn - k;
    const uint32_t *yl = y, *yh = y + k; size_t yln = k, yhn = yn - k;

    if (xln > SIZE_MAX - yln || xhn > SIZE_MAX - yhn) {
        nat_mul_basic(rd, x, xn, y, yn);
        return;
    }
    size_t z0n = xln + yln, z2n = xhn + yhn;
    size_t sxn = (xhn > xln ? xhn : xln) + 1;
    size_t syn = (yhn > yln ? yhn : yln) + 1;
    if (sxn == 0 || syn == 0 || sxn > SIZE_MAX - syn) {
        nat_mul_basic(rd, x, xn, y, yn);
        return;
    }
    size_t z1n = sxn + syn;

    uint32_t *z0 = (uint32_t *)calloc(z0n, 4);
    uint32_t *z2 = (uint32_t *)calloc(z2n, 4);
    uint32_t *sx = (uint32_t *)calloc(sxn, 4);
    uint32_t *sy = (uint32_t *)calloc(syn, 4);
    uint32_t *z1 = (uint32_t *)calloc(z1n, 4);
    if (!z0 || !z2 || !sx || !sy || !z1) {   /* fall back, never wrong */
        free(z0); free(z2); free(sx); free(sy); free(z1);
        nat_mul_basic(rd, x, xn, y, yn);
        return;
    }

    nat_mul(z0, xl, xln, yl, yln);           /* z0 = xl*yl */
    nat_mul(z2, xh, xhn, yh, yhn);           /* z2 = xh*yh */

    memcpy(sx, xl, xln * 4); nat_add_into(sx, sxn, xh, xhn);   /* sx = xl+xh */
    memcpy(sy, yl, yln * 4); nat_add_into(sy, syn, yh, yhn);   /* sy = yl+yh */
    nat_mul(z1, sx, sxn, sy, syn);           /* z1 = (xl+xh)(yl+yh) */
    nat_sub_into(z1, z1n, z0, z0n);          /* z1 -= z0 */
    nat_sub_into(z1, z1n, z2, z2n);          /* z1 -= z2 = xl*yh + xh*yl */

    nat_add_into(rd, xn + yn, z0, z0n);                 /* + z0       */
    nat_add_into(rd + k, xn + yn - k, z1, nat_efflen(z1, z1n));   /* + z1<<k */
    nat_add_into(rd + 2 * k, xn + yn - 2 * k, z2, z2n);          /* + z2<<2k */

    free(z0); free(z2); free(sx); free(sy); free(z1);
}

/*
 * Squaring x*x exploits symmetry: the n^2 cross products x[i]*x[j] come in
 * equal pairs (i,j) and (j,i), so only the i<j half is computed and the whole
 * off-diagonal sum is doubled once, then the n diagonal squares x[i]^2 are
 * added. That is ~n^2/2 word-multiplies instead of n^2 for a generic multiply.
 * rd[0..2n) must be pre-zeroed.
 */
static void nat_sqr_basic(uint32_t *rd, const uint32_t *x, size_t n) {
    /* Off-diagonal upper triangle: rd += sum_{i<j} x[i]*x[j] * B^(i+j). */
    for (size_t i = 0; i < n; i++) {
        uint64_t carry = 0, xi = x[i];
        size_t kk = 2 * i + 1;
        for (size_t j = i + 1; j < n; j++, kk++) {
            uint64_t p = xi * x[j] + rd[kk] + carry;
            rd[kk] = (uint32_t)p;
            carry = p >> 32;
        }
        while (carry) {
            uint64_t s = (uint64_t)rd[kk] + carry;
            rd[kk] = (uint32_t)s; carry = s >> 32; kk++;
        }
    }
    /* Double the triangle in place (2*T < x^2 < B^(2n), so no top overflow). */
    uint32_t c = 0;
    for (size_t kk = 0; kk < 2 * n; kk++) {
        uint32_t v = rd[kk];
        rd[kk] = (v << 1) | c;
        c = v >> 31;
    }
    /* Add the diagonal squares x[i]^2 at position 2i. */
    for (size_t i = 0; i < n; i++) {
        uint64_t xi = x[i];
        uint64_t sq = xi * xi;
        size_t kk = 2 * i;
        uint64_t s = (uint64_t)rd[kk] + (uint32_t)sq;
        rd[kk] = (uint32_t)s;
        uint64_t carry = (s >> 32) + (sq >> 32);
        kk++;
        while (carry) {
            uint64_t t = (uint64_t)rd[kk] + carry;
            rd[kk] = (uint32_t)t; carry = t >> 32; kk++;
        }
    }
}

/* rd[0..2n) = x*x. Karatsuba squaring above the threshold (z1 = (xl+xh)^2 -
 * xl^2 - xh^2 needs only 3 squarings), schoolbook below. rd pre-zeroed. */
static void nat_sqr(uint32_t *rd, const uint32_t *x, size_t n) {
    if (n == 0) return;
    if (n < NCI_KARATSUBA_SQR_THRESHOLD) { nat_sqr_basic(rd, x, n); return; }

    if (n >= (size_t)NCI_TOOM3_SQR_THRESHOLD) {  /* Toom-3 squaring (n > 4 always) */
        nat_toom3_sqr(rd, x, n, (n + 2) / 3);
        return;
    }

    size_t k = n / 2;                        /* split point, 1 <= k < n */
    const uint32_t *xl = x, *xh = x + k;
    size_t xln = k, xhn = n - k;

    if (xln > SIZE_MAX / 2 || xhn > SIZE_MAX / 2) {
        nat_sqr_basic(rd, x, n);
        return;
    }
    size_t z0n = 2 * xln, z2n = 2 * xhn;
    size_t sn = (xhn > xln ? xhn : xln) + 1;
    if (sn == 0 || sn > SIZE_MAX / 2) {
        nat_sqr_basic(rd, x, n);
        return;
    }
    size_t z1n = 2 * sn;

    uint32_t *z0 = (uint32_t *)calloc(z0n, 4);
    uint32_t *z2 = (uint32_t *)calloc(z2n, 4);
    uint32_t *sx = (uint32_t *)calloc(sn, 4);
    uint32_t *z1 = (uint32_t *)calloc(z1n, 4);
    if (!z0 || !z2 || !sx || !z1) {          /* fall back, never wrong */
        free(z0); free(z2); free(sx); free(z1);
        nat_sqr_basic(rd, x, n);
        return;
    }

    nat_sqr(z0, xl, xln);                     /* z0 = xl^2 */
    nat_sqr(z2, xh, xhn);                     /* z2 = xh^2 */
    memcpy(sx, xl, xln * 4); nat_add_into(sx, sn, xh, xhn);    /* sx = xl+xh */
    nat_sqr(z1, sx, sn);                      /* z1 = (xl+xh)^2 */
    nat_sub_into(z1, z1n, z0, z0n);           /* z1 -= z0 */
    nat_sub_into(z1, z1n, z2, z2n);           /* z1 -= z2 = 2*xl*xh */

    nat_add_into(rd, 2 * n, z0, z0n);                              /* + z0      */
    nat_add_into(rd + k, 2 * n - k, z1, nat_efflen(z1, z1n));      /* + z1<<k   */
    nat_add_into(rd + 2 * k, 2 * n - 2 * k, z2, z2n);             /* + z2<<2k  */

    free(z0); free(z2); free(sx); free(z1);
}

/* ------------------------------------------------------------------ *
 * Toom-Cook-3 multiplication and squaring (raw magnitudes).
 *
 * Each operand is a degree-2 polynomial in B = base^k (k words per part):
 *   X(t) = x0 + x1 t + x2 t^2 ,  Y(t) = y0 + y1 t + y2 t^2
 * with x0,x1 exactly k words and x2 the high part (< B). The product W = X*Y
 * has five coefficients c0..c4; they are recovered from evaluations at
 * t = 0, 1, -1, 2, inf and recomposed at t = B:
 *   p0 = W(0) = x0*y0 = c0          pinf = W(inf) = x2*y2 = c4
 *   p1 = W(1) , pm1 = W(-1) , p2 = W(2)
 *   c2 = (p1+pm1)/2 - c0 - c4
 *   A  = (p1-pm1)/2                  (= c1 + c3)
 *   Bv = (p2 - c0 - 4 c2 - 16 c4)/2  (= c1 + 4 c3)
 *   c3 = (Bv - A)/3 ;  c1 = A - c3
 * The five sub-products go through nat_mul / nat_sqr, so they recurse and drop
 * to Karatsuba/schoolbook once small; the linear evaluation/interpolation runs
 * on raw limb buffers (no per-op bigint allocation) so the asymptotic win is
 * not eaten by overhead. Only W(-1) is signed; the rest of the interpolation
 * stays nonnegative, so a single sign flag on pm1 suffices.
 * ------------------------------------------------------------------ */

/* Compare trimmed magnitudes: -1 / 0 / 1. */
static int mag_cmp(const uint32_t *a, size_t an, const uint32_t *b, size_t bn) {
    while (an > 0 && a[an - 1] == 0) an--;
    while (bn > 0 && b[bn - 1] == 0) bn--;
    if (an != bn) return an < bn ? -1 : 1;
    for (size_t i = an; i-- > 0; ) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* dst = a + b ; dst capacity >= max(an,bn)+1 ; returns trimmed length. */
static size_t mag_add(uint32_t *dst, const uint32_t *a, size_t an,
                      const uint32_t *b, size_t bn) {
    if (an < bn) { const uint32_t *t = a; a = b; b = t; size_t s = an; an = bn; bn = s; }
    memcpy(dst, a, an * 4); dst[an] = 0;
    nat_add_into(dst, an + 1, b, bn);
    return nat_efflen(dst, an + 1);
}

/* dst = |u - v| ; *sign = (u < v) ; dst capacity >= max(un,vn) ; returns len. */
static size_t mag_sub_signed(uint32_t *dst, const uint32_t *u, size_t un,
                             const uint32_t *v, size_t vn, int *sign) {
    int c = mag_cmp(u, un, v, vn);
    if (c == 0) { *sign = 0; return 0; }
    if (c > 0) { *sign = 0; memcpy(dst, u, un * 4); nat_sub_into(dst, un, v, vn); return nat_efflen(dst, un); }
    *sign = 1; memcpy(dst, v, vn * 4); nat_sub_into(dst, vn, u, un); return nat_efflen(dst, vn);
}

/* dst = src << bits (0 < bits < 32) ; dst capacity >= n+1 ; returns len. */
static size_t mag_shl_small(uint32_t *dst, const uint32_t *src, size_t n, unsigned bits) {
    uint32_t carry = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t v = ((uint64_t)src[i] << bits) | carry;
        dst[i] = (uint32_t)v; carry = (uint32_t)(v >> 32);
    }
    dst[n] = carry;
    return nat_efflen(dst, n + 1);
}

/* a >>= 1 over n words (value assumed even -> exact). */
static void mag_shr1(uint32_t *a, size_t n) {
    uint32_t carry = 0;
    for (size_t i = n; i-- > 0; ) { uint32_t v = a[i]; a[i] = (v >> 1) | (carry << 31); carry = v & 1; }
}

/* a /= 3 over n words (value assumed divisible by 3 -> exact). Hensel/Jebelean
 * exact division: multiply each limb by 3^-1 mod 2^32 (= 0xAAAAAAAB) low-to-high
 * with a borrow carried from the high half of q*3 — two 32x32 muls per limb and
 * no hardware divide, faster than the top-down 64/32 long division. */
static void mag_divexact3(uint32_t *a, size_t n) {
    const uint32_t inv3 = 0xAAAAAAABu;          /* 3 * inv3 == 1 (mod 2^32) */
    uint32_t h = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t s = a[i];
        uint32_t x = s - h;                     /* subtract incoming borrow */
        h = (x > s) ? 1u : 0u;                  /* unsigned underflow -> borrow */
        uint32_t q = x * inv3;                  /* q*3 == x (mod 2^32) */
        a[i] = q;
        h += (uint32_t)(((uint64_t)q * 3u) >> 32);   /* carry the high half */
    }
}

/* Shared interpolation + recompose for both multiply and square. Inputs are the
 * five evaluation products p0,p1,pm1,p2,pinf (each a pn-word buffer) with pm1's
 * sign in spm1; t1,t2,t3 are pn-word scratch buffers. rd[0..xn+yn) pre-zeroed.
 * Each coefficient c_i provably fits its B^i recompose window (c_i < base^slot),
 * since x*y < base^(xn+yn). */
static void toom3_interpolate(uint32_t *rd, size_t xn, size_t yn, size_t k,
                              uint32_t *p0, uint32_t *p1, uint32_t *pm1,
                              uint32_t *p2, uint32_t *pinf, int spm1,
                              size_t pn, uint32_t *t1, uint32_t *t2, uint32_t *t3) {
    size_t lp0 = nat_efflen(p0, pn), lpm1 = nat_efflen(pm1, pn);
    size_t lpinf = nat_efflen(pinf, pn);

    /* S = p1 + pm1 (= 2(c0+c2+c4)) in t1 ; D = p1 - pm1 (= 2(c1+c3)) in t2.
     * Both nonnegative; pm1's sign selects add vs sub. */
    memcpy(t1, p1, pn * 4);
    memcpy(t2, p1, pn * 4);
    if (spm1 == 0) { nat_add_into(t1, pn, pm1, lpm1); nat_sub_into(t2, pn, pm1, lpm1); }
    else           { nat_sub_into(t1, pn, pm1, lpm1); nat_add_into(t2, pn, pm1, lpm1); }

    mag_shr1(t1, pn);                    /* t1 = c0 + c2 + c4 */
    mag_shr1(t2, pn);                    /* t2 = A = c1 + c3  */
    nat_sub_into(t1, pn, p0, lp0);       /* t1 -= c0 */
    nat_sub_into(t1, pn, pinf, lpinf);   /* t1 = c2  */

    /* Bv = (p2 - c0 - 4 c2 - 16 c4)/2 into p1 (its old value is now in t1/t2). */
    memcpy(p1, p2, pn * 4);
    nat_sub_into(p1, pn, p0, lp0);
    size_t l = mag_shl_small(t3, t1, nat_efflen(t1, pn), 2);   /* 4 c2 */
    nat_sub_into(p1, pn, t3, l);
    l = mag_shl_small(t3, pinf, lpinf, 4);                     /* 16 c4 */
    nat_sub_into(p1, pn, t3, l);
    mag_shr1(p1, pn);                    /* p1 = Bv = c1 + 4 c3 */

    nat_sub_into(p1, pn, t2, nat_efflen(t2, pn));   /* p1 = Bv - A = 3 c3 */
    mag_divexact3(p1, pn);                            /* p1 = c3 */
    nat_sub_into(t2, pn, p1, nat_efflen(p1, pn));    /* t2 = A - c3 = c1 */

    /* Recompose: rd = c0 + c1 B + c2 B^2 + c3 B^3 + c4 B^4. */
    size_t tot = xn + yn;
    nat_add_into(rd,           tot,          p0,   lp0);                       /* c0 */
    nat_add_into(rd + k,       tot - k,      t2,   nat_efflen(t2, pn));        /* c1 */
    nat_add_into(rd + 2 * k,   tot - 2 * k,  t1,   nat_efflen(t1, pn));        /* c2 */
    nat_add_into(rd + 3 * k,   tot - 3 * k,  p1,   nat_efflen(p1, pn));        /* c3 */
    nat_add_into(rd + 4 * k,   tot - 4 * k,  pinf, lpinf);                     /* c4 */
}

static void nat_toom3(uint32_t *rd, const uint32_t *x, size_t xn,
                      const uint32_t *y, size_t yn, size_t k) {
    const uint32_t *x0 = x, *x1 = x + k, *x2 = x + 2 * k;
    const uint32_t *y0 = y, *y1 = y + k, *y2 = y + 2 * k;
    size_t x2n = xn - 2 * k, y2n = yn - 2 * k;       /* each in [1, k] */

    size_t ek = k + 2;                  /* evaluation-operand capacity */
    size_t pn = 2 * k + 4;              /* product / interpolation capacity */

    /* One zeroed scratch block, carved into the working buffers: a single
     * allocation per node (instead of ~15) keeps Toom-3's constant factor low
     * enough to beat Karatsuba near the crossover. */
    uint32_t *buf = NULL;
    if (ek <= SIZE_MAX / 7 && pn <= SIZE_MAX / 8 &&
        7 * ek <= SIZE_MAX - 8 * pn)
        buf = (uint32_t *)calloc(7 * ek + 8 * pn, 4);
    /* Must not call nat_mul: the caller already chose Toom-3, so that
     * re-enters this function and recurses until the stack dies. */
    if (!buf) { nat_mul_basic(rd, x, xn, y, yn); return; }
    uint32_t *ex1 = buf, *ex2 = ex1 + ek, *exm = ex2 + ek;
    uint32_t *ey1 = exm + ek, *ey2 = ey1 + ek, *eym = ey2 + ek, *tmp = eym + ek;
    uint32_t *p0 = tmp + ek, *p1 = p0 + pn, *pm1 = p1 + pn, *p2 = pm1 + pn;
    uint32_t *pinf = p2 + pn, *t1 = pinf + pn, *t2 = t1 + pn, *t3 = t2 + pn;

    /* ex1 = x0+x1+x2 ; ey1 = y0+y1+y2 */
    memcpy(ex1, x0, k * 4); nat_add_into(ex1, ek, x1, k); nat_add_into(ex1, ek, x2, x2n);
    memcpy(ey1, y0, k * 4); nat_add_into(ey1, ek, y1, k); nat_add_into(ey1, ek, y2, y2n);
    size_t lex1 = nat_efflen(ex1, ek), ley1 = nat_efflen(ey1, ek);

    /* ex2 = x0 + 2 x1 + 4 x2 ; ey2 likewise (Horner with small shifts). */
    size_t le = mag_shl_small(ex2, x2, x2n, 2);          /* 4 x2 */
    size_t lt = mag_shl_small(tmp, x1, k, 1);            /* 2 x1 */
    nat_add_into(ex2, ek, tmp, lt); nat_add_into(ex2, ek, x0, k);
    size_t lex2 = nat_efflen(ex2, ek); (void)le;
    le = mag_shl_small(ey2, y2, y2n, 2);
    lt = mag_shl_small(tmp, y1, k, 1);
    nat_add_into(ey2, ek, tmp, lt); nat_add_into(ey2, ek, y0, k);
    size_t ley2 = nat_efflen(ey2, ek); (void)le;

    /* exm = x0 + x2 - x1 (signed) ; eym = y0 + y2 - y1 (signed). */
    int sxm, sym;
    size_t ls = mag_add(tmp, x0, k, x2, x2n);
    size_t lexm = mag_sub_signed(exm, tmp, ls, x1, k, &sxm);
    ls = mag_add(tmp, y0, k, y2, y2n);
    size_t leym = mag_sub_signed(eym, tmp, ls, y1, k, &sym);

    /* Five sub-products (recurse through nat_mul). */
    nat_mul(p0,   x0,  k,    y0,  k);
    nat_mul(p1,   ex1, lex1, ey1, ley1);
    nat_mul(pm1,  exm, lexm, eym, leym);
    nat_mul(p2,   ex2, lex2, ey2, ley2);
    nat_mul(pinf, x2,  x2n,  y2,  y2n);
    int spm1 = sxm ^ sym;

    toom3_interpolate(rd, xn, yn, k, p0, p1, pm1, p2, pinf, spm1, pn, t1, t2, t3);

    free(buf);
}

static void nat_toom3_sqr(uint32_t *rd, const uint32_t *x, size_t n, size_t k) {
    const uint32_t *x0 = x, *x1 = x + k, *x2 = x + 2 * k;
    size_t x2n = n - 2 * k;                          /* in [1, k] */

    size_t ek = k + 2;
    size_t pn = 2 * k + 4;

    uint32_t *buf = NULL;
    if (ek <= SIZE_MAX / 4 && pn <= SIZE_MAX / 8 &&
        4 * ek <= SIZE_MAX - 8 * pn)
        buf = (uint32_t *)calloc(4 * ek + 8 * pn, 4);
    /* Must not call nat_sqr: that re-enters Toom-3-sqr on the same n. */
    if (!buf) { nat_sqr_basic(rd, x, n); return; }
    uint32_t *ex1 = buf, *ex2 = ex1 + ek, *exm = ex2 + ek, *tmp = exm + ek;
    uint32_t *p0 = tmp + ek, *p1 = p0 + pn, *pm1 = p1 + pn, *p2 = pm1 + pn;
    uint32_t *pinf = p2 + pn, *t1 = pinf + pn, *t2 = t1 + pn, *t3 = t2 + pn;

    memcpy(ex1, x0, k * 4); nat_add_into(ex1, ek, x1, k); nat_add_into(ex1, ek, x2, x2n);
    size_t lex1 = nat_efflen(ex1, ek);

    size_t le = mag_shl_small(ex2, x2, x2n, 2);
    size_t lt = mag_shl_small(tmp, x1, k, 1);
    nat_add_into(ex2, ek, tmp, lt); nat_add_into(ex2, ek, x0, k);
    size_t lex2 = nat_efflen(ex2, ek); (void)le;

    int sxm;
    size_t ls = mag_add(tmp, x0, k, x2, x2n);
    size_t lexm = mag_sub_signed(exm, tmp, ls, x1, k, &sxm);

    /* Squares: every sub-product is nonnegative, so pm1's sign is always +. */
    nat_sqr(p0,   x0,  k);
    nat_sqr(p1,   ex1, lex1);
    nat_sqr(pm1,  exm, lexm);
    nat_sqr(p2,   ex2, lex2);
    nat_sqr(pinf, x2,  x2n);

    toom3_interpolate(rd, n, n, k, p0, p1, pm1, p2, pinf, 0, pn, t1, t2, t3);

    free(buf);
}

void neverc_bigint_mul(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    if (x->len == 0 || y->len == 0) {
        z->len = 0; z->neg = 0; return;
    }

    neverc_bigint_t result;
    neverc_bigint_init(&result);
    if (x->len > SIZE_MAX - y->len) return;
    size_t rlen = x->len + y->len;
    if (!ensure_cap(&result, rlen)) return;
    memset(result.digits, 0, rlen * sizeof(uint32_t));
    result.len = rlen;

    if (x == y) {                             /* dedicated squaring path */
        nat_sqr(result.digits, x->digits, x->len);
        result.neg = 0;
    } else {
        nat_mul(result.digits, x->digits, x->len, y->digits, y->len);
        result.neg = (x->neg != y->neg);
    }

    trim(&result);
    neverc_bigint_free(z);
    *z = result;
}

/*
 * Knuth Algorithm D — word-wise long division (TAOCP 4.3.1), base 2^32.
 * Replaces the old bit-by-bit shift/subtract (which was ~O(32 n^2) and
 * allocated on every bit). Computes unsigned magnitudes only; the caller
 * applies signs. Requires y->len >= 2 and |x| >= |y| (both trimmed).
 * The multiply-and-subtract / add-back uses the Hacker's Delight divmnu
 * borrow formulation.
 */
static void nat_divmod(neverc_bigint_t *q, neverc_bigint_t *r,
                       const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t n = y->len;
    /* Top limb 0: shifting it left never sets the high bit (infinite loop). */
    if (n < 2 || x->len < n || !y->digits || y->digits[n - 1] == 0)
        return;
    size_t m = x->len - n;

    /* D1: normalize so the divisor's top word has its high bit set. */
    unsigned s = 0;
    { uint32_t t = y->digits[n - 1]; while (s < 32 && !(t & 0x80000000u)) { t <<= 1; s++; } }
    if (s == 32)
        return;

    if (n > SIZE_MAX / sizeof(uint32_t) ||
        m > SIZE_MAX - n - 2 ||
        m + n + 2 > SIZE_MAX / sizeof(uint32_t) ||
        m > SIZE_MAX / sizeof(uint32_t) - 1)
        return;
    uint32_t *v  = (uint32_t *)malloc(n * sizeof(uint32_t));
    uint32_t *u  = (uint32_t *)malloc((m + n + 2) * sizeof(uint32_t));
    uint32_t *qd = (uint32_t *)calloc(m + 1, sizeof(uint32_t));
    if (!v || !u || !qd) {
        note_oom();
        free(v); free(u); free(qd);
        return;
    }

    { uint32_t c = 0;                         /* v = y << s (fits in n words) */
      for (size_t i = 0; i < n; i++) {
          uint64_t cur = ((uint64_t)y->digits[i] << s) | c;
          v[i] = (uint32_t)cur; c = (uint32_t)(cur >> 32);
      } }
    { uint32_t c = 0;                         /* u = x << s (m+n+1 words) */
      for (size_t i = 0; i < x->len; i++) {
          uint64_t cur = ((uint64_t)x->digits[i] << s) | c;
          u[i] = (uint32_t)cur; c = (uint32_t)(cur >> 32);
      }
      u[x->len] = c; }

    uint32_t vn1 = v[n - 1], vn2 = v[n - 2];
    for (size_t jj = m + 1; jj-- > 0; ) {
        size_t j = jj;
        /* D3: estimate the quotient digit. */
        uint64_t num  = ((uint64_t)u[j + n] << 32) | u[j + n - 1];
        uint64_t qhat = num / vn1;
        uint64_t rhat = num - qhat * vn1;
        while (qhat > 0xFFFFFFFFULL ||
               qhat * vn2 > ((rhat << 32) | u[j + n - 2])) {
            qhat--; rhat += vn1;
            if (rhat > 0xFFFFFFFFULL) break;
        }
        /* D4: multiply and subtract. */
        int64_t k = 0, t;
        for (size_t i = 0; i < n; i++) {
            uint64_t p = qhat * v[i];
            t = (int64_t)u[j + i] - k - (int64_t)(uint32_t)(p & 0xFFFFFFFFULL);
            u[j + i] = (uint32_t)t;
            k = (int64_t)(uint32_t)(p >> 32) - (t >> 32);
        }
        t = (int64_t)u[j + n] - k;
        u[j + n] = (uint32_t)t;
        /* D5/D6: if we subtracted too much, add the divisor back. */
        if (t < 0) {
            qhat--;
            uint64_t c = 0;
            for (size_t i = 0; i < n; i++) {
                uint64_t s2 = (uint64_t)u[j + i] + v[i] + c;
                u[j + i] = (uint32_t)s2; c = s2 >> 32;
            }
            u[j + n] = (uint32_t)((uint64_t)u[j + n] + c);
        }
        qd[j] = (uint32_t)qhat;
    }

    int wrote_q = !q;
    int wrote_r = !r;
    if (q) {
        if (ensure_cap(q, m + 1)) {
            memcpy(q->digits, qd, (m + 1) * sizeof(uint32_t));
            q->len = m + 1; q->neg = 0; trim(q);
            wrote_q = 1;
        }
    }
    if (r) {
        if (ensure_cap(r, n)) {
            uint32_t c = 0;                   /* D8: remainder = u[0..n) >> s */
            for (size_t i = n; i-- > 0; ) {
                uint32_t cur = u[i];
                r->digits[i] = s ? ((cur >> s) | c) : cur;
                c = s ? (cur << (32 - s)) : 0;
            }
            r->len = n; r->neg = 0; trim(r);
            wrote_r = 1;
        }
    }
    if (!wrote_q || !wrote_r) {
        if (q) { q->len = 0; q->neg = 0; }
        if (r) { r->len = 0; r->neg = 0; }
    }
    free(v); free(u); free(qd);
}

/* ---------------------------------------------------------------------------
 * Burnikel-Ziegler recursive division ("Fast Recursive Division", 1998).
 *
 * Knuth's nat_divmod above is O(n*m); for balanced operands that is O(n^2),
 * the same asymptotic the schoolbook multiply had before nat_mul got
 * Karatsuba. Burnikel-Ziegler is the divisor-side counterpart: it reduces a
 * 2n/n division to a handful of n/2 divisions plus Karatsuba multiplies,
 * giving O(M(n) log n). This makes div / mod / and base-10 formatting of very
 * large integers subquadratic, matching Go/Java/GMP.
 *
 * The routines work on nonnegative magnitudes via the public bigint ops and
 * fall back to Knuth (nat_divmod) at the recursion base case. Block sizes are
 * tracked explicitly in 32-bit words because bigints auto-trim leading zeros,
 * so an "n-word block" cannot be recovered from ->len.
 * ------------------------------------------------------------------------- */

/* Divisor word count at/below which the recursion bottoms out to Knuth.
 * Correctness is independent of this value (any >= 2 works); it only trades
 * recursion overhead against Knuth's lower constant. Overridable for fuzzing
 * (set to 2 to exercise the deepest recursion paths). */
#ifndef NCI_BZ_THRESHOLD
#define NCI_BZ_THRESHOLD 48
#endif
/* Minimum divisor word count for neverc_bigint_div to choose BZ over Knuth.
 * Tuned to the measured crossover: below this Knuth's lower constant wins; at
 * and above it BZ's subquadratic scaling pulls ahead (and keeps widening). */
#ifndef NCI_BZ_DIV_MIN
#define NCI_BZ_DIV_MIN 256
#endif

/* out = words [lo, lo+cnt) of src, zero-extended, as a trimmed magnitude. */
static void bn_block(neverc_bigint_t *out, const neverc_bigint_t *src,
                     size_t lo, size_t cnt) {
    out->neg = 0;
    if (lo >= src->len || cnt == 0) { out->len = 0; return; }
    size_t hi = cnt > src->len - lo ? src->len : lo + cnt;
    size_t m = hi - lo;
    if (!ensure_cap(out, m)) return;
    memcpy(out->digits, src->digits + lo, m * sizeof(uint32_t));
    out->len = m;
    trim(out);
}

/* z <<= words * 32 bits (whole-word left shift).
 * Do not go through neverc_bigint_lsh: that takes an unsigned bit count, so
 * words >= 2^27 silently wraps the shift on 32-bit unsigned. */
static void bn_shl_words(neverc_bigint_t *z, size_t words) {
    if (z->len == 0 || words == 0) return;
    if (words > SIZE_MAX - z->len) return;
    size_t newlen = z->len + words;
    if (!ensure_cap(z, newlen)) return;
    memmove(z->digits + words, z->digits, z->len * sizeof(uint32_t));
    memset(z->digits, 0, words * sizeof(uint32_t));
    z->len = newlen;
}

/* q = floor(x / y), r = x mod y for nonnegative magnitudes (y != 0).
 * No BZ recursion — Knuth / single-word only. Handles x < y and x == y. */
static void mag_divmod_basic(neverc_bigint_t *q, neverc_bigint_t *r,
                             const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t xv, yv;
    mag_view(&xv, x);
    mag_view(&yv, y);
    x = &xv;
    y = &yv;
    if (y->len == 0) {                         /* fail-closed:  q = r = 0 */
        if (q) { q->len = 0; q->neg = 0; }
        if (r) { r->len = 0; r->neg = 0; }
        return;
    }
    int c = abs_cmp(x, y);
    if (c < 0) {                                   /* x < y: q = 0, r = x */
        if (q) { q->len = 0; q->neg = 0; }
        if (r) { neverc_bigint_set(r, x); r->neg = 0; }
        return;
    }
    if (c == 0) {                                  /* x == y: q = 1, r = 0 */
        if (q) neverc_bigint_set_uint64(q, 1);
        if (r) { r->len = 0; r->neg = 0; }
        return;
    }
    if (y->len == 1) {                             /* single-word divisor */
        uint32_t d = y->digits[0];
        if (d == 0) {                              /* untrimmed 0 */
            if (q) { q->len = 0; q->neg = 0; }
            if (r) { r->len = 0; r->neg = 0; }
            return;
        }
        neverc_bigint_t quot;
        neverc_bigint_init(&quot);
        if (!ensure_cap(&quot, x->len)) {
            neverc_bigint_free(&quot);
            return;
        }
        quot.len = x->len;
        uint64_t rem = 0;
        for (size_t i = x->len; i > 0; i--) {
            uint64_t cur = (rem << 32) | x->digits[i - 1];
            quot.digits[i - 1] = (uint32_t)(cur / d);
            rem = cur % d;
        }
        trim(&quot);
        if (q) { neverc_bigint_free(q); *q = quot; } else neverc_bigint_free(&quot);
        if (r) neverc_bigint_set_uint64(r, rem);
        return;
    }
    neverc_bigint_t quot, rem;                      /* y->len >= 2, x > y */
    neverc_bigint_init(&quot);
    neverc_bigint_init(&rem);
    nat_divmod(&quot, &rem, x, y);
    if (q) { neverc_bigint_free(q); *q = quot; } else neverc_bigint_free(&quot);
    if (r) { neverc_bigint_free(r); *r = rem; } else neverc_bigint_free(&rem);
}

static void bz_div2n1n(neverc_bigint_t *q, neverc_bigint_t *r,
                       const neverc_bigint_t *a, const neverc_bigint_t *b, size_t n);

/* a (3n words) / b (2n words) -> q (n words), r (2n words); a = q*b + r.
 * Precondition: a < b * base^n (so the quotient fits in n words). */
static void bz_div3n2n(neverc_bigint_t *q, neverc_bigint_t *r,
                       const neverc_bigint_t *a, const neverc_bigint_t *b, size_t n) {
    neverc_bigint_t b1, b2, a12, a3, a1, qh, rh, d, t;
    neverc_bigint_init(&b1); neverc_bigint_init(&b2);
    neverc_bigint_init(&a12); neverc_bigint_init(&a3); neverc_bigint_init(&a1);
    neverc_bigint_init(&qh); neverc_bigint_init(&rh);
    neverc_bigint_init(&d); neverc_bigint_init(&t);

    bn_block(&b1, b, n, n);          /* high n words of divisor */
    bn_block(&b2, b, 0, n);          /* low  n words of divisor */
    bn_block(&a12, a, n, 2 * n);     /* high 2n words of a */
    bn_block(&a3, a, 0, n);          /* low  n words of a */
    bn_block(&a1, a, 2 * n, n);      /* top  n words of a */

    if (abs_cmp(&a1, &b1) < 0) {
        bz_div2n1n(&qh, &rh, &a12, &b1, n);          /* qh = a12 / b1 */
    } else {
        if (!ensure_cap(&qh, n)) goto cleanup;       /* qh = base^n - 1 */
        for (size_t i = 0; i < n; i++) qh.digits[i] = 0xFFFFFFFFu;
        qh.len = n; qh.neg = 0; trim(&qh);
        neverc_bigint_set(&t, &b1);                  /* rh = a12 + b1 - (b1<<n) */
        bn_shl_words(&t, n);
        neverc_bigint_add(&rh, &a12, &b1);
        neverc_bigint_sub(&rh, &rh, &t);
    }

    neverc_bigint_mul(&d, &qh, &b2);                 /* d = qh * b2 */
    bn_shl_words(&rh, n);                            /* r = rh*base^n + a3 - d */
    neverc_bigint_add(&rh, &rh, &a3);
    neverc_bigint_sub(&rh, &rh, &d);

    /* At most twice. Cap the loop so an OOM no-op add cannot spin. */
    for (int k = 0; k < 2 && rh.neg && rh.len > 0; k++) {
        neverc_bigint_add(&rh, &rh, b);
        neverc_bigint_set_uint64(&t, 1);
        neverc_bigint_sub(&qh, &qh, &t);
    }
    if (rh.neg && rh.len > 0)
        goto cleanup;

    neverc_bigint_set(q, &qh);
    neverc_bigint_set(r, &rh);

cleanup:
    neverc_bigint_free(&b1); neverc_bigint_free(&b2);
    neverc_bigint_free(&a12); neverc_bigint_free(&a3); neverc_bigint_free(&a1);
    neverc_bigint_free(&qh); neverc_bigint_free(&rh);
    neverc_bigint_free(&d); neverc_bigint_free(&t);
}

/* a (<= 2n words) / b (n words, top bit set) -> q (n words), r (n words).
 * Precondition: a < b * base^n. */
static void bz_div2n1n(neverc_bigint_t *q, neverc_bigint_t *r,
                       const neverc_bigint_t *a, const neverc_bigint_t *b, size_t n) {
    if ((n & 1) || n < NCI_BZ_THRESHOLD) {
        mag_divmod_basic(q, r, a, b);
        return;
    }
    size_t half = n >> 1;
    neverc_bigint_t a_hi3, a4, q1, r1, q2, r2, z;
    neverc_bigint_init(&a_hi3); neverc_bigint_init(&a4);
    neverc_bigint_init(&q1); neverc_bigint_init(&r1);
    neverc_bigint_init(&q2); neverc_bigint_init(&r2);
    neverc_bigint_init(&z);

    bn_block(&a_hi3, a, half, 3 * half);   /* top 3*half words */
    bn_block(&a4, a, 0, half);             /* low half words */

    bz_div3n2n(&q1, &r1, &a_hi3, b, half);

    neverc_bigint_set(&z, &r1);            /* z = (r1 << half) + a4 */
    bn_shl_words(&z, half);
    neverc_bigint_add(&z, &z, &a4);
    bz_div3n2n(&q2, &r2, &z, b, half);

    neverc_bigint_set(q, &q1);             /* q = (q1 << half) + q2 */
    bn_shl_words(q, half);
    neverc_bigint_add(q, q, &q2);
    neverc_bigint_set(r, &r2);

    neverc_bigint_free(&a_hi3); neverc_bigint_free(&a4);
    neverc_bigint_free(&q1); neverc_bigint_free(&r1);
    neverc_bigint_free(&q2); neverc_bigint_free(&r2);
    neverc_bigint_free(&z);
}

/* q = floor(|x| / |y|), r = |x| mod |y|, via Burnikel-Ziegler. Outputs are
 * nonnegative. Precondition: |x| >= |y| and y->len >= 2. */
static void bz_divmod(neverc_bigint_t *q, neverc_bigint_t *r,
                      const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t s = y->len;

    /* Block size n (words): n = j*m with m the smallest power of two > s/THR,
     * so n >= s and n is even (lets the recursion split). */
    size_t q0 = s / (size_t)NCI_BZ_THRESHOLD;
    size_t m = 1;
    while (m <= q0) {
        if (m > SIZE_MAX / 2) return; /* next shift would wrap to 0 and spin */
        m <<= 1;
    }
    if (m > 0 && s > SIZE_MAX - (m - 1)) return;
    size_t j = (s + m - 1) / m;
    if (m > 0 && j > SIZE_MAX / m) return;
    size_t n = j * m;
    if (n > SIZE_MAX / 32)
        return;
    size_t n32 = n * 32;

    size_t ybits = (size_t)neverc_bigint_bit_len(y);
    size_t sigma = (n32 > ybits) ? (n32 - ybits) : 0;

    neverc_bigint_t ys, xs, z, blk, qi, ri, qq, sh;
    neverc_bigint_init(&ys); neverc_bigint_init(&xs);
    neverc_bigint_init(&z); neverc_bigint_init(&blk);
    neverc_bigint_init(&qi); neverc_bigint_init(&ri);
    neverc_bigint_init(&qq); neverc_bigint_init(&sh);

    neverc_bigint_lsh(&ys, y, (unsigned)sigma);   /* normalize: exactly n words */
    neverc_bigint_lsh(&xs, x, (unsigned)sigma);
    ys.neg = 0; xs.neg = 0;

    size_t xbits = (size_t)neverc_bigint_bit_len(&xs);
    size_t t = (xbits + n32) / n32;               /* number of n-word blocks */
    if (t < 2) t = 2;

    bn_block(&z, &xs, (t - 1) * n, n);            /* top block (< ys) */

    for (size_t ii = t - 1; ii-- > 0; ) {
        bn_shl_words(&z, n);                       /* z = z*base^n + block(ii) */
        bn_block(&blk, &xs, ii * n, n);
        neverc_bigint_add(&z, &z, &blk);
        bz_div2n1n(&qi, &ri, &z, &ys, n);
        neverc_bigint_set(&z, &ri);
        neverc_bigint_set(&sh, &qi);              /* quotient += qi << (ii*n) */
        bn_shl_words(&sh, ii * n);
        neverc_bigint_add(&qq, &qq, &sh);
    }

    if (q) { neverc_bigint_set(q, &qq); q->neg = 0; }
    if (r) { neverc_bigint_rsh(r, &z, (unsigned)sigma); r->neg = 0; }

    neverc_bigint_free(&ys); neverc_bigint_free(&xs);
    neverc_bigint_free(&z); neverc_bigint_free(&blk);
    neverc_bigint_free(&qi); neverc_bigint_free(&ri);
    neverc_bigint_free(&qq); neverc_bigint_free(&sh);
}

void neverc_bigint_div(neverc_bigint_t *q, neverc_bigint_t *r,
                       const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t quot, rem, xv, yv;
    neverc_bigint_init(&quot);
    neverc_bigint_init(&rem);
    mag_view(&xv, x);
    mag_view(&yv, y);
    x = &xv;
    y = &yv;
    int xneg = x->neg;
    int yneg = y->neg;

    if (y->len == 0) {
        /* Go panics; fail closed with q = r = 0. */
    } else {
        int c = abs_cmp(x, y);
        if (c < 0) {
            if (!bigint_set_checked(&rem, x)) {
                neverc_bigint_free(&quot);
                neverc_bigint_free(&rem);
                return;
            }
        } else if (c == 0) {
            neverc_bigint_set_int64(&quot, 1);
            if (quot.len == 0) {
                neverc_bigint_free(&quot);
                neverc_bigint_free(&rem);
                return;
            }
            quot.neg = (xneg != yneg);
        } else if (y->len == 1) {
            uint32_t d = y->digits[0];
            if (d == 0) {
                /* untrimmed zero: leave q = r = 0 */
            } else if (!ensure_cap(&quot, x->len)) {
                neverc_bigint_free(&quot);
                neverc_bigint_free(&rem);
                return;
            } else {
                quot.len = x->len;
                uint64_t remv = 0;
                for (size_t i = x->len; i > 0; i--) {
                    uint64_t cur = (remv << 32) | x->digits[i - 1];
                    quot.digits[i - 1] = (uint32_t)(cur / d);
                    remv = cur % d;
                }
                trim(&quot);
                quot.neg = (xneg != yneg) && quot.len > 0;
                neverc_bigint_set_uint64(&rem, remv);
                rem.neg = xneg && remv > 0;
            }
        } else {
            if (y->len >= NCI_BZ_DIV_MIN)
                bz_divmod(&quot, &rem, x, y);
            else
                nat_divmod(&quot, &rem, x, y);
            /* |x| > |y| always yields |q| >= 1. An empty quotient means
             * the algorithm could not allocate; leave dest unchanged. */
            if (quot.len == 0) {
                neverc_bigint_free(&quot);
                neverc_bigint_free(&rem);
                return;
            }
            quot.neg = (xneg != yneg) && quot.len > 0;
            rem.neg = xneg && rem.len > 0;
        }
    }

    if (q) {
        neverc_bigint_free(q);
        *q = quot;
        neverc_bigint_init(&quot);
    }
    if (r && r != q) {
        neverc_bigint_free(r);
        *r = rem;
        neverc_bigint_init(&rem);
    }
    neverc_bigint_free(&quot);
    neverc_bigint_free(&rem);
}

void neverc_bigint_mod(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *m) {
    neverc_bigint_t rem, modulus, mv;
    neverc_bigint_init(&rem);
    mag_view(&mv, m);
    if (mv.len == 0) {
        /* Go panics on mod 0; fail closed with 0. */
        neverc_bigint_free(z);
        *z = rem;
        return;
    }
    neverc_bigint_init(&modulus);
    if (!bigint_set_checked(&modulus, m)) {
        neverc_bigint_free(&modulus);
        neverc_bigint_free(&rem);
        return;
    }
    int oom = 0;
    bigint_oom = &oom;
    neverc_bigint_div(NULL, &rem, x, &modulus);
    /* Euclidean remainder is in [0, |m|). Adding m when m is negative
     * makes a negative remainder more negative; add |m| instead. */
    if (!oom && rem.neg && rem.len > 0) {
        if (modulus.neg)
            neverc_bigint_sub(&rem, &rem, &modulus);
        else
            neverc_bigint_add(&rem, &rem, &modulus);
    }
    if (oom || (rem.neg && rem.len > 0)) {
        bigint_oom = NULL;
        neverc_bigint_free(&modulus);
        neverc_bigint_free(&rem);
        return;
    }
    bigint_oom = NULL;
    neverc_bigint_free(z);
    *z = rem;
    neverc_bigint_free(&modulus);
}

void neverc_bigint_neg(neverc_bigint_t *z, const neverc_bigint_t *x) {
    if (!bigint_set_checked(z, x)) return;
    if (z->len > 0) z->neg = !z->neg;
}

void neverc_bigint_abs(neverc_bigint_t *z, const neverc_bigint_t *x) {
    if (!bigint_set_checked(z, x)) return;
    z->neg = 0;
}

/* Magnitude bitwise helpers. Signs are cleared; callers apply Go math/big
 * two's-complement identities (And/Or/Xor/Rsh/Bit). */
static int mag_and(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len < y->len ? x->len : y->len;
    if (!ensure_cap(z, m)) return 0;
    for (size_t i = 0; i < m; i++)
        z->digits[i] = x->digits[i] & y->digits[i];
    z->len = m;
    z->neg = 0;
    trim(z);
    return 1;
}

static int mag_or(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len > y->len ? x->len : y->len;
    if (!ensure_cap(z, m)) return 0;
    for (size_t i = 0; i < m; i++) {
        uint32_t a = i < x->len ? x->digits[i] : 0;
        uint32_t b = i < y->len ? y->digits[i] : 0;
        z->digits[i] = a | b;
    }
    z->len = m;
    z->neg = 0;
    trim(z);
    return 1;
}

static int mag_xor(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    size_t m = x->len > y->len ? x->len : y->len;
    if (!ensure_cap(z, m)) return 0;
    for (size_t i = 0; i < m; i++) {
        uint32_t a = i < x->len ? x->digits[i] : 0;
        uint32_t b = i < y->len ? y->digits[i] : 0;
        z->digits[i] = a ^ b;
    }
    z->len = m;
    z->neg = 0;
    trim(z);
    return 1;
}

static int mag_andnot(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    if (!ensure_cap(z, x->len)) return 0;
    for (size_t i = 0; i < x->len; i++) {
        uint32_t b = i < y->len ? y->digits[i] : 0;
        z->digits[i] = x->digits[i] & ~b;
    }
    z->len = x->len;
    z->neg = 0;
    trim(z);
    return 1;
}

static int mag_sub1(neverc_bigint_t *z, const neverc_bigint_t *x) {
    size_t n = x->len;
    if (n == 0) {
        z->len = 0;
        z->neg = 0;
        return 1;
    }
    if (z != x && !ensure_cap(z, n)) return 0;
    uint32_t borrow = 1;
    for (size_t i = 0; i < n; i++) {
        uint32_t xi = (z == x) ? z->digits[i] : x->digits[i];
        z->digits[i] = xi - borrow;
        borrow = xi < borrow;
    }
    z->len = n;
    z->neg = 0;
    trim(z);
    return 1;
}

static int mag_add1(neverc_bigint_t *z, const neverc_bigint_t *x) {
    size_t n = x->len;
    if (!ensure_cap(z, n + 1)) return 0;
    const uint32_t *src = (z == x) ? z->digits : x->digits;
    uint64_t carry = 1;
    for (size_t i = 0; i < n; i++) {
        uint64_t r = (uint64_t)src[i] + carry;
        z->digits[i] = (uint32_t)r;
        carry = r >> 32;
    }
    if (carry)
        z->digits[n] = 1;
    z->len = n + (carry ? 1 : 0);
    z->neg = 0;
    trim(z);
    return 1;
}

static int mag_rsh(neverc_bigint_t *z, const neverc_bigint_t *x, unsigned n) {
    unsigned words = n / 32;
    unsigned bits = n % 32;
    if (words >= x->len) {
        z->len = 0;
        z->neg = 0;
        return 1;
    }
    size_t newlen = x->len - words;
    neverc_bigint_t tmp;
    neverc_bigint_init(&tmp);
    if (!ensure_cap(&tmp, newlen)) return 0;
    tmp.len = newlen;
    tmp.neg = 0;
    for (size_t i = 0; i < newlen; i++) {
        tmp.digits[i] = x->digits[i + words] >> bits;
        if (bits > 0 && i + words + 1 < x->len)
            tmp.digits[i] |= x->digits[i + words + 1] << (32 - bits);
    }
    trim(&tmp);
    neverc_bigint_free(z);
    *z = tmp;
    return 1;
}

static int bitwise_snap(neverc_bigint_t *tx, neverc_bigint_t *ty,
                        neverc_bigint_t *z,
                        const neverc_bigint_t **x, const neverc_bigint_t **y) {
    if (z == *x) {
        if (!bigint_set_checked(tx, *x)) return 0;
        *x = tx;
    }
    if (z == *y) {
        if (!bigint_set_checked(ty, *y)) return 0;
        *y = ty;
    }
    return 1;
}

void neverc_bigint_lsh(neverc_bigint_t *z, const neverc_bigint_t *x, unsigned n) {
    if (x->len == 0) { z->len = 0; z->neg = 0; return; }
    unsigned words = n / 32;
    unsigned bits = n % 32;
    if ((size_t)words > SIZE_MAX - x->len - 1U) return;
    size_t newlen = x->len + (size_t)words + 1U;

    neverc_bigint_t tmp;
    neverc_bigint_init(&tmp);
    if (!ensure_cap(&tmp, newlen)) return;
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
    neverc_bigint_t t;
    if (!x->neg) {
        mag_rsh(z, x, n);
        return;
    }
    /* Go: z = -((((|x|-1) >> n) + 1))  so -1 >> n stays -1. */
    neverc_bigint_init(&t);
    if (!mag_sub1(&t, x) || !mag_rsh(&t, &t, n) || !mag_add1(z, &t)) {
        neverc_bigint_free(&t);
        return;
    }
    z->neg = z->len > 0;
    neverc_bigint_free(&t);
}

void neverc_bigint_and(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t tx, ty, x1, y1;
    neverc_bigint_init(&tx); neverc_bigint_init(&ty);
    neverc_bigint_init(&x1); neverc_bigint_init(&y1);
    if (!bitwise_snap(&tx, &ty, z, &x, &y)) goto done;
    if (x->neg == y->neg) {
        if (x->neg) {
            if (!mag_sub1(&x1, x) || !mag_sub1(&y1, y) ||
                !mag_or(z, &x1, &y1) || !mag_add1(z, z))
                goto done;
            z->neg = z->len > 0;
        } else if (!mag_and(z, x, y)) {
            goto done;
        }
    } else {
        if (x->neg) { const neverc_bigint_t *t = x; x = y; y = t; }
        if (!mag_sub1(&y1, y) || !mag_andnot(z, x, &y1))
            goto done;
    }
done:
    neverc_bigint_free(&tx); neverc_bigint_free(&ty);
    neverc_bigint_free(&x1); neverc_bigint_free(&y1);
}

void neverc_bigint_or(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t tx, ty, x1, y1;
    neverc_bigint_init(&tx); neverc_bigint_init(&ty);
    neverc_bigint_init(&x1); neverc_bigint_init(&y1);
    if (!bitwise_snap(&tx, &ty, z, &x, &y)) goto done;
    if (x->neg == y->neg) {
        if (x->neg) {
            if (!mag_sub1(&x1, x) || !mag_sub1(&y1, y) ||
                !mag_and(z, &x1, &y1) || !mag_add1(z, z))
                goto done;
            z->neg = z->len > 0;
        } else if (!mag_or(z, x, y)) {
            goto done;
        }
    } else {
        if (x->neg) { const neverc_bigint_t *t = x; x = y; y = t; }
        if (!mag_sub1(&y1, y) || !mag_andnot(z, &y1, x) || !mag_add1(z, z))
            goto done;
        z->neg = z->len > 0;
    }
done:
    neverc_bigint_free(&tx); neverc_bigint_free(&ty);
    neverc_bigint_free(&x1); neverc_bigint_free(&y1);
}

void neverc_bigint_xor(neverc_bigint_t *z, const neverc_bigint_t *x, const neverc_bigint_t *y) {
    neverc_bigint_t tx, ty, x1, y1;
    neverc_bigint_init(&tx); neverc_bigint_init(&ty);
    neverc_bigint_init(&x1); neverc_bigint_init(&y1);
    if (!bitwise_snap(&tx, &ty, z, &x, &y)) goto done;
    if (x->neg == y->neg) {
        if (x->neg) {
            if (!mag_sub1(&x1, x) || !mag_sub1(&y1, y) || !mag_xor(z, &x1, &y1))
                goto done;
        } else if (!mag_xor(z, x, y)) {
            goto done;
        }
    } else {
        if (x->neg) { const neverc_bigint_t *t = x; x = y; y = t; }
        if (!mag_sub1(&y1, y) || !mag_xor(z, x, &y1) || !mag_add1(z, z))
            goto done;
        z->neg = z->len > 0;
    }
done:
    neverc_bigint_free(&tx); neverc_bigint_free(&ty);
    neverc_bigint_free(&x1); neverc_bigint_free(&y1);
}

int neverc_bigint_bit(const neverc_bigint_t *x, unsigned i) {
    unsigned word = i / 32;
    unsigned bit = i % 32;
    int b = 0;
    if ((size_t)word < x->len)
        b = (int)((x->digits[word] >> bit) & 1);
    if (!x->neg) return b;
    /* bit i of -x is ((|x|-1).bit(i)) ^ 1 */
    int lower_zero = 1;
    unsigned w;
    for (w = 0; w < word; w++) {
        if ((size_t)w < x->len && x->digits[w] != 0) {
            lower_zero = 0;
            break;
        }
    }
    if (lower_zero) {
        uint32_t dw = (size_t)word < x->len ? x->digits[word] : 0;
        uint32_t lowmask = bit ? ((1u << bit) - 1u) : 0;
        if (dw & lowmask)
            lower_zero = 0;
    }
    return b ^ lower_zero ^ 1;
}

/*
 * Fixed-window (2^w-ary) square-and-multiply. Used directly for plain (non-
 * modular) exponentiation and as the fallback when the modulus is even (where
 * Montgomery does not apply). Precomputing g[i] = base^i and consuming w
 * exponent bits per step cuts the multiply count from ~bits/2 to ~bits/w.
 */
static void exp_window(neverc_bigint_t *z, const neverc_bigint_t *base,
                       const neverc_bigint_t *exp, const neverc_bigint_t *m) {
    int domod = (m && m->len > 0);

    neverc_bigint_t result, b, e;
    neverc_bigint_init(&result);
    neverc_bigint_init(&b);
    neverc_bigint_init(&e);
    neverc_bigint_set_int64(&result, 1);
    neverc_bigint_set(&b, base);
    b.neg = 0;
    neverc_bigint_set(&e, exp);
    if (domod) {
        neverc_bigint_mod(&b, &b, m);
        /* x^0 ≡ 1 (mod m) must still land in [0,|m|): 1 mod 1 == 0. */
        neverc_bigint_mod(&result, &result, m);
    }

    int bits = neverc_bigint_bit_len(&e);

    int w;                                   /* window width vs exponent size */
    if      (bits >= 2048) w = 7;
    else if (bits >= 768)  w = 6;
    else if (bits >= 256)  w = 5;
    else if (bits >= 96)   w = 4;
    else if (bits >= 32)   w = 3;
    else if (bits >= 8)    w = 2;
    else                   w = 1;

    int tbl = 1 << w;
    neverc_bigint_t *g = (neverc_bigint_t *)malloc((size_t)tbl * sizeof(*g));
    if (!g) {                                /* degrade to binary on OOM */
        for (int i = 0; i < bits; i++) {
            if (neverc_bigint_bit(&e, (unsigned)i)) {
                neverc_bigint_mul(&result, &result, &b);
                if (domod) neverc_bigint_mod(&result, &result, m);
            }
            neverc_bigint_mul(&b, &b, &b);
            if (domod) neverc_bigint_mod(&b, &b, m);
        }
        neverc_bigint_free(z); *z = result;
        neverc_bigint_free(&b); neverc_bigint_free(&e);
        return;
    }

    for (int i = 0; i < tbl; i++) neverc_bigint_init(&g[i]);
    neverc_bigint_set_int64(&g[0], 1);       /* g[i] = base^i (mod m) */
    if (tbl > 1) neverc_bigint_set(&g[1], &b);
    for (int i = 2; i < tbl; i++) {
        neverc_bigint_mul(&g[i], &g[i - 1], &b);
        if (domod) neverc_bigint_mod(&g[i], &g[i], m);
    }

    int nwin = (bits + w - 1) / w;           /* MSB-first window scan */
    for (int idx = nwin - 1; idx >= 0; idx--) {
        for (int s = 0; s < w; s++) {
            neverc_bigint_mul(&result, &result, &result);
            if (domod) neverc_bigint_mod(&result, &result, m);
        }
        int win = 0;
        for (int j = 0; j < w; j++)
            win |= neverc_bigint_bit(&e, (unsigned)(idx * w + j)) << j;
        if (win) {
            neverc_bigint_mul(&result, &result, &g[win]);
            if (domod) neverc_bigint_mod(&result, &result, m);
        }
    }

    for (int i = 0; i < tbl; i++) neverc_bigint_free(&g[i]);
    free(g);

    neverc_bigint_free(z);
    *z = result;
    neverc_bigint_free(&b);
    neverc_bigint_free(&e);
}

/* ------------------------------------------------------------------ *
 * Montgomery modular exponentiation (odd modulus).
 *
 * The window method above still pays for a trial-division reduction after every
 * multiply/square. Montgomery arithmetic replaces those divisions with cheap
 * shifts: operands are mapped to the residue x*R mod m (R = 2^(32n)), products
 * x*y*R^-1 mod m are formed with multiply-add-shift only (CIOS), and the final
 * value is mapped back at the end. This is the standard fast path for RSA / DSA
 * / elliptic-curve modular exponentiation.
 * ------------------------------------------------------------------ */

/* -m[0]^-1 mod 2^32 via Newton's iteration (requires m[0] odd). */
static uint32_t mont_n0inv(uint32_t m0) {
    uint32_t inv = m0;                 /* correct mod 2^3 */
    inv *= 2u - m0 * inv;              /* mod 2^6  */
    inv *= 2u - m0 * inv;              /* mod 2^12 */
    inv *= 2u - m0 * inv;              /* mod 2^24 */
    inv *= 2u - m0 * inv;              /* mod 2^48 -> 2^32 */
    return (uint32_t)(0u - inv);
}

/* out = a*b*R^-1 mod m (CIOS). a,b in [0,m); out in [0,m). t is scratch[n+2]. */
static void mont_mul(uint32_t *out, const uint32_t *a, const uint32_t *b,
                     const uint32_t *m, size_t n, uint32_t n0inv, uint32_t *t) {
    for (size_t i = 0; i < n + 2; i++) t[i] = 0;

    for (size_t i = 0; i < n; i++) {
        uint64_t C = 0, bi = b[i];
        for (size_t j = 0; j < n; j++) {
            uint64_t s = (uint64_t)t[j] + (uint64_t)a[j] * bi + C;
            t[j] = (uint32_t)s; C = s >> 32;
        }
        uint64_t s = (uint64_t)t[n] + C;
        t[n] = (uint32_t)s; t[n + 1] = (uint32_t)(s >> 32);

        uint32_t mm = (uint32_t)((uint64_t)t[0] * n0inv);
        s = (uint64_t)t[0] + (uint64_t)mm * m[0];
        C = s >> 32;                   /* low word is zero by construction */
        for (size_t j = 1; j < n; j++) {
            s = (uint64_t)t[j] + (uint64_t)mm * m[j] + C;
            t[j - 1] = (uint32_t)s; C = s >> 32;
        }
        s = (uint64_t)t[n] + C;
        t[n - 1] = (uint32_t)s;
        t[n] = (uint32_t)((uint64_t)t[n + 1] + (s >> 32));
    }

    /* t in [0,2m): conditionally subtract m to land in [0,m). */
    int ge = t[n] != 0;
    if (!ge) {
        ge = 1;
        for (size_t j = n; j-- > 0; ) {
            if (t[j] != m[j]) { ge = t[j] > m[j]; break; }
        }
    }
    if (ge) {
        int64_t borrow = 0;
        for (size_t j = 0; j < n; j++) {
            int64_t d = (int64_t)t[j] - (int64_t)m[j] - borrow;
            if (d < 0) { d += (int64_t)1 << 32; borrow = 1; } else borrow = 0;
            out[j] = (uint32_t)d;
        }
    } else {
        for (size_t j = 0; j < n; j++) out[j] = t[j];
    }
}

/* Returns 0 on success (z set), -1 if it could not run (caller falls back). */
static int exp_montgomery(neverc_bigint_t *z, const neverc_bigint_t *base,
                          const neverc_bigint_t *exp, const neverc_bigint_t *m) {
    size_t n = m->len;
    /* R² = 2^(64n); the shift count is unsigned, so huge moduli fall back. */
    if (n == 0 || n > (size_t)0xFFFFFFFFu / 64u)
        return -1;
    const uint32_t *M = m->digits;
    uint32_t n0inv = mont_n0inv(M[0]);

    /* R2 = 2^(64n) mod m, and base reduced into [0,m). */
    neverc_bigint_t R2, bmod;
    neverc_bigint_init(&R2);
    neverc_bigint_init(&bmod);
    neverc_bigint_set_int64(&R2, 1);
    neverc_bigint_lsh(&R2, &R2, (unsigned)(64u * (unsigned)n));
    neverc_bigint_mod(&R2, &R2, m);
    neverc_bigint_abs(&bmod, base);
    neverc_bigint_mod(&bmod, &bmod, m);

    int bits = neverc_bigint_bit_len(exp);
    int w;
    if      (bits >= 2048) w = 6;
    else if (bits >= 512)  w = 5;
    else if (bits >= 128)  w = 4;
    else if (bits >= 32)   w = 3;
    else if (bits >= 8)    w = 2;
    else                   w = 1;
    int tbl = 1 << w;

    if (n > SIZE_MAX / sizeof(uint32_t) - 2 ||
        (n > 0 && (size_t)tbl > SIZE_MAX / n)) {
        neverc_bigint_free(&R2); neverc_bigint_free(&bmod);
        return -1;
    }
    uint32_t *t     = (uint32_t *)malloc((n + 2) * sizeof(uint32_t));
    uint32_t *R2w   = (uint32_t *)calloc(n, sizeof(uint32_t));
    uint32_t *basew = (uint32_t *)calloc(n, sizeof(uint32_t));
    uint32_t *amont = (uint32_t *)calloc(n, sizeof(uint32_t));
    uint32_t *onew  = (uint32_t *)calloc(n, sizeof(uint32_t));
    uint32_t *buf0  = (uint32_t *)calloc(n, sizeof(uint32_t));
    uint32_t *buf1  = (uint32_t *)calloc(n, sizeof(uint32_t));
    uint32_t *g     = (uint32_t *)calloc((size_t)tbl * n, sizeof(uint32_t));
    if (!t || !R2w || !basew || !amont || !onew || !buf0 || !buf1 || !g) {
        note_oom();
        free(t); free(R2w); free(basew); free(amont);
        free(onew); free(buf0); free(buf1); free(g);
        neverc_bigint_free(&R2); neverc_bigint_free(&bmod);
        return -1;
    }

    for (size_t i = 0; i < R2.len && i < n; i++)   R2w[i]   = R2.digits[i];
    for (size_t i = 0; i < bmod.len && i < n; i++) basew[i] = bmod.digits[i];
    onew[0] = 1;

    mont_mul(amont, basew, R2w, M, n, n0inv, t);   /* base -> Montgomery form */
    mont_mul(g, onew, R2w, M, n, n0inv, t);        /* g[0] = R mod m (Mont 1) */
    if (tbl > 1) memcpy(g + n, amont, n * sizeof(uint32_t));
    for (int i = 2; i < tbl; i++)
        mont_mul(g + (size_t)i * n, g + (size_t)(i - 1) * n, amont, M, n, n0inv, t);

    uint32_t *cur = buf0, *other = buf1;
    memcpy(cur, g, n * sizeof(uint32_t));          /* start at Montgomery 1 */

    int nwin = (bits + w - 1) / w;
    for (int idx = nwin - 1; idx >= 0; idx--) {
        for (int s = 0; s < w; s++) {
            mont_mul(other, cur, cur, M, n, n0inv, t);
            uint32_t *tmp = cur; cur = other; other = tmp;
        }
        int win = 0;
        for (int j = 0; j < w; j++)
            win |= neverc_bigint_bit(exp, (unsigned)(idx * w + j)) << j;
        if (win) {
            mont_mul(other, cur, g + (size_t)win * n, M, n, n0inv, t);
            uint32_t *tmp = cur; cur = other; other = tmp;
        }
    }

    mont_mul(other, cur, onew, M, n, n0inv, t);    /* map out of Montgomery */

    if (!ensure_cap(z, n)) {
        free(t); free(R2w); free(basew); free(amont);
        free(onew); free(buf0); free(buf1); free(g);
        neverc_bigint_free(&R2); neverc_bigint_free(&bmod);
        return -1;
    }
    memcpy(z->digits, other, n * sizeof(uint32_t));
    z->len = n; z->neg = 0;
    trim(z);

    free(t); free(R2w); free(basew); free(amont);
    free(onew); free(buf0); free(buf1); free(g);
    neverc_bigint_free(&R2); neverc_bigint_free(&bmod);
    return 0;
}

static int bigint_modinv(neverc_bigint_t *r, const neverc_bigint_t *a,
                         const neverc_bigint_t *m) {
    /* Go Int.Exp / ModInverse ignore the sign of m (and of a). Running
     * extended Euclid on a negative modulus yields gcd = -1, so 2^-1 mod -5
     * was reported non-invertible even though 2*3 ≡ 1 (mod 5). */
    neverc_bigint_t aa, mm, old_r, rr, old_s, s, q, tmp, prod;
    neverc_bigint_init(&aa); neverc_bigint_init(&mm);
    neverc_bigint_init(&old_r); neverc_bigint_init(&rr);
    neverc_bigint_init(&old_s); neverc_bigint_init(&s);
    neverc_bigint_init(&q); neverc_bigint_init(&tmp);
    neverc_bigint_init(&prod);

    neverc_bigint_abs(&aa, a);
    neverc_bigint_abs(&mm, m);
    int invertible = 0;
    if (mm.len > 0) {
        neverc_bigint_set(&old_r, &aa);
        neverc_bigint_set(&rr, &mm);
        neverc_bigint_set_int64(&old_s, 1);
        neverc_bigint_set_int64(&s, 0);

        while (!neverc_bigint_is_zero(&rr)) {
            neverc_bigint_div(&q, &tmp, &old_r, &rr);
            neverc_bigint_set(&old_r, &rr);
            neverc_bigint_set(&rr, &tmp);
            neverc_bigint_mul(&prod, &q, &s);
            neverc_bigint_sub(&tmp, &old_s, &prod);
            neverc_bigint_set(&old_s, &s);
            neverc_bigint_set(&s, &tmp);
        }

        neverc_bigint_set_int64(&tmp, 1);
        invertible = (neverc_bigint_cmp(&old_r, &tmp) == 0);
        if (invertible)
            neverc_bigint_mod(r, &old_s, &mm);
    }

    neverc_bigint_free(&aa); neverc_bigint_free(&mm);
    neverc_bigint_free(&old_r); neverc_bigint_free(&rr);
    neverc_bigint_free(&old_s); neverc_bigint_free(&s);
    neverc_bigint_free(&q); neverc_bigint_free(&tmp);
    neverc_bigint_free(&prod);
    return invertible ? 0 : -1;
}

void neverc_bigint_exp(neverc_bigint_t *z, const neverc_bigint_t *base,
                       const neverc_bigint_t *exp, const neverc_bigint_t *m) {
    /* Leading zero limbs made m look nonzero (window reduced mod 0 → 0
     * instead of x**y) and inflated Montgomery's word count. */
    neverc_bigint_t mv;
    if (m) {
        mag_view(&mv, m);
        m = &mv;
    }
    /* Go Int.Exp: y < 0 and m == nil (or 0) yields 1. With a modulus,
     * invert x first (after reducing into [0,|m|)), raise that inverse
     * to |y|, then apply the original negative-base sign when |y| is odd.
     * Inverting after Exp(x,|y|,m) flips the sign for odd |y|. */
    if (exp->neg) {
        if (!m || m->len == 0) {
            neverc_bigint_set_int64(z, 1);
            return;
        }
        neverc_bigint_t mag, reduced, inv, pow;
        neverc_bigint_init(&mag);
        neverc_bigint_init(&reduced);
        neverc_bigint_init(&inv);
        neverc_bigint_init(&pow);
        neverc_bigint_abs(&mag, exp);
        neverc_bigint_mod(&reduced, base, m);
        if (bigint_modinv(&inv, &reduced, m) == 0) {
            neverc_bigint_exp(&pow, &inv, &mag, m);
            if (base->neg && mag.len > 0 && (mag.digits[0] & 1u) &&
                pow.len > 0) {
                neverc_bigint_t mm;
                neverc_bigint_init(&mm);
                neverc_bigint_set(&mm, m);
                mm.neg = 0;
                neverc_bigint_sub(&pow, &mm, &pow);
                neverc_bigint_free(&mm);
            }
            neverc_bigint_set(z, &pow);
        }
        neverc_bigint_free(&mag);
        neverc_bigint_free(&reduced);
        neverc_bigint_free(&inv);
        neverc_bigint_free(&pow);
        return;
    }
    int base_neg = base->neg && base->len > 0;
    int exp_odd = exp->len > 0 && (exp->digits[0] & 1u);

    /* Compute into a local so dest==modulus (or dest==base) cannot clobber
     * inputs used for the negative-base adjustment, or the modulus itself. */
    neverc_bigint_t result;
    neverc_bigint_init(&result);

    /* Odd modulus -> Montgomery (division-free); otherwise window method. */
    if (m && m->len > 0 && (m->digits[0] & 1u)) {
        if (exp_montgomery(&result, base, exp, m) != 0)
            exp_window(&result, base, exp, m);
    } else {
        exp_window(&result, base, exp, m);
    }
    if (base_neg && exp_odd) {
        if (m && m->len > 0) {
            if (result.len > 0) {
                neverc_bigint_t mm;
                neverc_bigint_init(&mm);
                neverc_bigint_set(&mm, m);
                mm.neg = 0;
                neverc_bigint_sub(&result, &mm, &result);
                neverc_bigint_free(&mm);
            }
        } else if (result.len > 0) {
            result.neg = 1;
        }
    }
    neverc_bigint_free(z);
    *z = result;
}

static int nlz32(uint32_t x) { return x ? __builtin_clz(x) : 32; }

static void bigint_swap(neverc_bigint_t *a, neverc_bigint_t *b) {
    neverc_bigint_t t = *a; *a = *b; *b = t;
}

/* Single-word remainder: a mod w (a nonneg magnitude). */
static uint32_t nat_mod_word(const neverc_bigint_t *a, uint32_t w) {
    if (w == 0) return 0;
    uint64_t rem = 0;
    for (size_t i = a->len; i-- > 0; )
        rem = ((rem << 32) | a->digits[i]) % w;
    return (uint32_t)rem;
}

/* One multiprecision Euclidean step on magnitudes: (a, b) <- (b, a mod b).
 * Requires a >= b > 0. */
static void euclid_step(neverc_bigint_t *a, neverc_bigint_t *b) {
    neverc_bigint_t r;
    neverc_bigint_init(&r);
    neverc_bigint_div(NULL, &r, a, b);   /* r = a mod b (both nonneg) */
    bigint_swap(a, b);                   /* a <- old b */
    neverc_bigint_free(b);               /* drop old a */
    *b = r;                              /* b <- r (ownership moved) */
}

/* Apply the Lehmer 2x2 cofactor matrix:
 *   (a, b) <- (A*a + B*b, C*a + D*b)
 * Cofactors are signed; Knuth's algorithm guarantees both results are >= 0. */
static void combine2x2(neverc_bigint_t *a, neverc_bigint_t *b,
                       int64_t A, int64_t B, int64_t C, int64_t D) {
    neverc_bigint_t ca, cb, t1, t2, na, nb;
    neverc_bigint_init(&ca); neverc_bigint_init(&cb);
    neverc_bigint_init(&t1); neverc_bigint_init(&t2);
    neverc_bigint_init(&na); neverc_bigint_init(&nb);

    neverc_bigint_set_int64(&ca, A);
    neverc_bigint_mul(&t1, &ca, a);          /* A*a */
    neverc_bigint_set_int64(&cb, B);
    neverc_bigint_mul(&t2, &cb, b);          /* B*b */
    neverc_bigint_add(&na, &t1, &t2);        /* na = A*a + B*b */

    neverc_bigint_set_int64(&ca, C);
    neverc_bigint_mul(&t1, &ca, a);          /* C*a */
    neverc_bigint_set_int64(&cb, D);
    neverc_bigint_mul(&t2, &cb, b);          /* D*b */
    neverc_bigint_add(&nb, &t1, &t2);        /* nb = C*a + D*b */

    na.neg = 0; nb.neg = 0;                  /* nonneg by construction */
    neverc_bigint_free(a); *a = na;
    neverc_bigint_free(b); *b = nb;

    neverc_bigint_free(&ca); neverc_bigint_free(&cb);
    neverc_bigint_free(&t1); neverc_bigint_free(&t2);
}

/*
 * Lehmer's GCD (Knuth TAOCP 4.5.2, Algorithm L). The old code ran the plain
 * Euclidean algorithm, doing a full multiprecision division at every step.
 * Lehmer instead derives a batch of quotients from the leading words using
 * single-precision arithmetic, packs them into a 2x2 transform, and applies it
 * with one multiprecision combine — replacing ~O(words) costly divisions per
 * outer step with cheap word-sized work, the way introsort batches comparisons.
 *
 * Leading words are normalized so the divisor's high bit is set (quotients stay
 * tiny); the two-quotient test only accepts a quotient proven correct despite
 * the unknown low-order bits; all products are guarded to fit in int64.
 */
void neverc_bigint_gcd(neverc_bigint_t *z, const neverc_bigint_t *x,
                       const neverc_bigint_t *y) {
    neverc_bigint_t a, b;
    neverc_bigint_init(&a);
    neverc_bigint_init(&b);
    neverc_bigint_abs(&a, x);
    neverc_bigint_abs(&b, y);
    trim(&a);
    trim(&b);
    if (abs_cmp(&a, &b) < 0) bigint_swap(&a, &b);   /* a >= b */

    while (b.len > 1) {
        size_t n = a.len;                           /* n >= b.len >= 2 */
        int h = nlz32(a.digits[n - 1]);

        /* uh: top 32 bits of a after shifting left by h (high bit set). */
        uint32_t uh = (uint32_t)(((uint64_t)a.digits[n - 1] << h) |
                                 (h ? ((uint64_t)a.digits[n - 2] >> (32 - h)) : 0));
        /* vh: the matching 32-bit window of b at a's leading position. */
        uint32_t vh;
        if (b.len == n) {
            vh = (uint32_t)(((uint64_t)b.digits[n - 1] << h) |
                            (h ? ((uint64_t)b.digits[n - 2] >> (32 - h)) : 0));
        } else if (b.len == n - 1) {
            vh = (uint32_t)(h ? ((uint64_t)b.digits[n - 2] >> (32 - h)) : 0);
        } else {
            vh = 0;                                 /* a >> b: no usable window */
        }
        if (vh == 0) { euclid_step(&a, &b); continue; }

        int64_t A = 1, B = 0, C = 0, D = 1;
        uint32_t x0 = uh, y0 = vh;
        for (;;) {
            int64_t yC = (int64_t)y0 + C;
            int64_t yD = (int64_t)y0 + D;
            if (yC <= 0 || yD <= 0) break;
            int64_t q  = ((int64_t)x0 + A) / yC;
            int64_t q2 = ((int64_t)x0 + B) / yD;
            if (q != q2) break;                     /* quotient not yet certain */

            int64_t ac = C < 0 ? -C : C, ad = D < 0 ? -D : D;
            if ((ac && q > 0x7FFFFFFFFFFFFFFFLL / ac) ||
                (ad && q > 0x7FFFFFFFFFFFFFFFLL / ad))
                break;                              /* guard int64 overflow */

            int64_t t;
            t = A - q * C; A = C; C = t;
            t = B - q * D; B = D; D = t;
            int64_t ny = (int64_t)x0 - q * (int64_t)y0;   /* 0 <= ny < y0 */
            x0 = y0; y0 = (uint32_t)ny;
            if (y0 == 0) break;
        }

        if (B == 0) {
            euclid_step(&a, &b);                    /* single precision stalled */
        } else {
            combine2x2(&a, &b, A, B, C, D);
            if (abs_cmp(&a, &b) < 0) bigint_swap(&a, &b);
        }
    }

    if (b.len == 1) {                               /* finish in single words */
        uint32_t bb = b.digits[0];
        uint32_t aa = nat_mod_word(&a, bb);
        while (aa) { uint32_t t = bb % aa; bb = aa; aa = t; }
        neverc_bigint_set_uint64(&a, bb);
    }
    /* else b.len == 0: a already holds the gcd */

    a.neg = 0;
    neverc_bigint_set(z, &a);
    neverc_bigint_free(&a);
    neverc_bigint_free(&b);
}

/* Word count at/above which neverc_bigint_string switches to the divide-and-
 * conquer conversion. Tuned to the measured crossover (~1500 decimal digits);
 * below it the single-word loop's lower constant wins. */
#ifndef NCI_BIGSTR_DC_MIN
#define NCI_BIGSTR_DC_MIN 160
#endif

/* Recursive divide-and-conquer base conversion: write exactly nch*k base
 * digits of v (with 0 <= v < chunk^nch), most-significant first and left
 * zero-padded, into dst. pw2[j] = chunk^(2^j). Each split is one division by a
 * precomputed power; with Burnikel-Ziegler division this is subquadratic,
 * mirroring the D&C parser in set_string (which combines with Karatsuba). */
static void bigint_emit_chunks(char *dst, const neverc_bigint_t *v, size_t nch,
                               const neverc_bigint_t *pw2, int k, int base,
                               const char *digits) {
    if (nch == 1) {
        uint32_t val = (v->len > 0) ? v->digits[0] : 0;   /* v < chunk fits a word */
        for (int d = k - 1; d >= 0; d--) {
            dst[d] = digits[val % (uint32_t)base];
            val /= (uint32_t)base;
        }
        return;
    }
    size_t b = 1; int j = 0;
    while (b * 2 < nch) { b *= 2; j++; }      /* b = largest power of two < nch */
    neverc_bigint_t hi, lo;
    neverc_bigint_init(&hi); neverc_bigint_init(&lo);
    neverc_bigint_div(&hi, &lo, v, &pw2[j]);   /* v = hi*chunk^b + lo */
    bigint_emit_chunks(dst, &hi, nch - b, pw2, k, base, digits);
    bigint_emit_chunks(dst + (nch - b) * (size_t)k, &lo, b, pw2, k, base, digits);
    neverc_bigint_free(&hi); neverc_bigint_free(&lo);
}

int neverc_bigint_string(const neverc_bigint_t *x, int base, char *buf, size_t cap) {
    if (base < 2 || base > 36 || !buf || cap == 0) return -1;
    if (x->len == 0) {
        if (cap < 2) return -1;
        buf[0] = '0'; buf[1] = '\0';
        return 1;
    }

    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

    /* Largest power of base that still fits in a single word, so each division
     * is the fast single-word path and yields k digits at once (~k-fold fewer
     * divisions than the old digit-at-a-time loop). */
    uint32_t chunk = 1;
    int k = 0;
    while ((uint64_t)chunk * (uint32_t)base <= 0xFFFFFFFFULL) {
        chunk *= (uint32_t)base;
        k++;
    }

    /* Large inputs: subquadratic divide-and-conquer conversion. Splits v by
     * precomputed chunk^(2^j) powers, so the work is Karatsuba/BZ-driven
     * instead of O(n^2) single-word divisions — the format-side counterpart of
     * the D&C parser. */
    if (x->len >= (size_t)NCI_BIGSTR_DC_MIN) {
        int cb = 0; { uint32_t c = chunk; while (c) { cb++; c >>= 1; } }
        size_t denom = (cb > 1) ? (size_t)(cb - 1) : 1;
        /* bit_len saturates at INT_MAX; word count does not, so M must
         * come from len or D&C drops high digits on huge values. */
        if (x->len > SIZE_MAX / 32) {
            /* fall through to the linear path's overflow check */
        } else {
        size_t B = x->len * 32;
        size_t M = (B + denom - 1) / denom;
        if (M < 1) M = 1;

        neverc_bigint_t pw2[64];
        int np = 0;
        int dc_ok = 1;
        neverc_bigint_init(&pw2[0]);
        neverc_bigint_set_uint64(&pw2[0], chunk);
        np = 1;
        while (np < 63 && ((size_t)1 << np) < M) {
            neverc_bigint_init(&pw2[np]);
            neverc_bigint_mul(&pw2[np], &pw2[np - 1], &pw2[np - 1]);
            if (pw2[np].len == 0) {
                dc_ok = 0;
                np++;
                break;
            }
            np++;
        }

        char *dc = (dc_ok && k > 0 && M <= SIZE_MAX / (size_t)k)
                   ? (char *)malloc(M * (size_t)k) : NULL;
        if (dc) {
            neverc_bigint_t v2;
            neverc_bigint_init(&v2);
            neverc_bigint_abs(&v2, x);
            bigint_emit_chunks(dc, &v2, M, pw2, k, base, digits);
            neverc_bigint_free(&v2);

            size_t total = M * (size_t)k, lead = 0;  /* strip leading zeros */
            while (lead + 1 < total && dc[lead] == '0') lead++;
            size_t ndig = total - lead;

            size_t needed = ndig + (x->neg ? 1 : 0) + 1;
            if (needed > cap) {
                for (int i = 0; i < np; i++) neverc_bigint_free(&pw2[i]);
                free(dc);
                return -1;
            }
            int out = 0;
            if (x->neg) buf[out++] = '-';
            memcpy(buf + out, dc + lead, ndig);
            out += (int)ndig;
            buf[out] = '\0';
            for (int i = 0; i < np; i++) neverc_bigint_free(&pw2[i]);
            free(dc);
            return out;
        }
        for (int i = 0; i < np; i++) neverc_bigint_free(&pw2[i]);
        /* malloc/OOM: fall through to the simple single-word loop */
        }
    }

    /* Bound by word count, not bit_len(): that API saturates at INT_MAX. */
    if (x->len > (SIZE_MAX - 2 * (size_t)k - 8) / 32) return -1;
    size_t tmpcap = x->len * 32 + 2 * (size_t)k + 8;
    char *tmp = (char *)malloc(tmpcap);
    if (!tmp) return -1;
    size_t pos = 0;

    neverc_bigint_t v;
    neverc_bigint_init(&v);
    neverc_bigint_abs(&v, x);

    while (v.len > 0) {
        uint32_t rem = 0;                    /* v = v / chunk, rem = v % chunk */
        for (size_t i = v.len; i-- > 0; ) {
            uint64_t cur = ((uint64_t)rem << 32) | v.digits[i];
            v.digits[i] = (uint32_t)(cur / chunk);
            rem = (uint32_t)(cur % chunk);
        }
        while (v.len > 0 && v.digits[v.len - 1] == 0) v.len--;

        if (v.len > 0) {                     /* interior chunk: emit k digits */
            for (int j = 0; j < k; j++) { tmp[pos++] = digits[rem % (uint32_t)base]; rem /= (uint32_t)base; }
        } else {                             /* most-significant chunk: no pad */
            do { tmp[pos++] = digits[rem % (uint32_t)base]; rem /= (uint32_t)base; } while (rem);
        }
    }

    size_t needed = pos + (x->neg ? 1 : 0) + 1;
    if (needed > cap) {
        neverc_bigint_free(&v);
        free(tmp);
        return -1;
    }

    int out = 0;
    if (x->neg) buf[out++] = '-';
    for (size_t i = pos; i-- > 0; )
        buf[out++] = tmp[i];
    buf[out] = '\0';

    neverc_bigint_free(&v);
    free(tmp);
    return out;
}
