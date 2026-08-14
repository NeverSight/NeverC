#include "neverc/std/strconv.h"
#include "decimal.h"
#include "pow10_eisel.h"
#include <string.h>
#include <stdint.h>

/*
 * Correctly-rounded string -> double parser.
 *
 * Three layers, fastest first, each provably correct or it defers:
 *   1. Clinger exact path  — when the mantissa and 10^exp are both exactly
 *      representable, one FP multiply/divide is exact.
 *   2. Eisel-Lemire        — 128-bit fixed-point multiply against a power-of-ten
 *      table yields the correctly-rounded result for >99.9% of inputs. It
 *      *only* returns a value when it can prove it is correctly rounded;
 *      otherwise it defers. (Lemire, "Number Parsing at a Gigabyte per
 *      Second", 2021.)
 *   3. Big-decimal fallback — an exact decimal-to-binary conversion with
 *      round-to-nearest-even. Handles the rare ambiguous cases, subnormals,
 *      and >19-significant-digit inputs that Eisel-Lemire defers on. This is
 *      the same algorithm used by Go's strconv and guarantees a correctly
 *      rounded answer for every input.
 *
 * The previous implementation claimed Eisel-Lemire but actually did naive FP
 * scaling, which mis-rounded ~51% of round-trippable doubles (up to 6 ULP).
 */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

static double nc_make_inf(int neg) {
    uint64_t b = neg ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL;
    double f; memcpy(&f, &b, 8); return f;
}
static double nc_make_nan(void) {
    uint64_t b = 0x7FF8000000000001ULL;
    double f; memcpy(&f, &b, 8); return f;
}
static double nc_from_bits(uint64_t b) { double f; memcpy(&f, &b, 8); return f; }

/* IEEE-754 binary64 parameters (NC_MANT_BITS/EXP_BITS/EXP_BIAS) come from
 * decimal.h. 10^19 - 1 < 2^64 bounds the fast-path mantissa. */
#define NC_MAX_MANT_DIGITS 19

/* Exact powers of ten (all representable in a double). */
static const double float64pow10[] = {
    1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,
    1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,
    1e20,1e21,1e22
};

/* ------------------------------------------------------------------ *
 * Layer 1: Clinger exact path
 * ------------------------------------------------------------------ */
static int atof64exact(uint64_t mantissa, int exp, int neg, double *out) {
    if (mantissa >> NC_MANT_BITS != 0) return 0;
    double f = (double)mantissa;
    if (neg) f = -f;
    if (exp == 0) { *out = f; return 1; }
    if (exp > 0 && exp <= 15 + 22) {
        if (exp > 22) { f *= float64pow10[exp - 22]; exp = 22; }
        if (f > 1e15 || f < -1e15) return 0;
        *out = f * float64pow10[exp];
        return 1;
    }
    if (exp < 0 && exp >= -22) { *out = f / float64pow10[-exp]; return 1; }
    return 0;
}

/* ------------------------------------------------------------------ *
 * Layer 2: Eisel-Lemire
 * ------------------------------------------------------------------ */
static inline void nci_mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
#ifdef __SIZEOF_INT128__
    __uint128_t r = (__uint128_t)a * b;
    *lo = (uint64_t)r; *hi = (uint64_t)(r >> 64);
#else
    uint64_t ah = a >> 32, al = (uint32_t)a, bh = b >> 32, bl = (uint32_t)b;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    *lo = (ll & 0xffffffffULL) | (mid << 32);
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
#endif
}

/* Returns 1 and the correctly-rounded double in *out, or 0 to defer. */
static int eisel_lemire64(uint64_t man, int exp10, int neg, double *out) {
    if (man == 0) { *out = neg ? -0.0 : 0.0; return 1; }
    if (exp10 < NCI_POW10_MIN_EXP10 || exp10 > NCI_POW10_MAX_EXP10) return 0;

    int clz = __builtin_clzll(man);
    man <<= clz;

    uint64_t ret_exp2 = (uint64_t)((217706LL * (int64_t)exp10) >> 16)
                      + 64 + (uint64_t)(-NC_EXP_BIAS) - (uint64_t)clz;

    const uint64_t *pw = nci_pow10_eisel[exp10 - NCI_POW10_MIN_EXP10]; /* {lo, hi} */
    uint64_t xhi, xlo;
    nci_mul64(man, pw[1], &xhi, &xlo);

    /* If the product is close to a rounding boundary, refine with the low
     * 64 bits of the 128-bit power. */
    if ((xhi & 0x1FF) == 0x1FF && (xlo + man < man)) {
        uint64_t yhi, ylo;
        nci_mul64(man, pw[0], &yhi, &ylo);
        uint64_t mhi = xhi, mlo = xlo + yhi;
        if (mlo < xlo) mhi++;
        if ((mhi & 0x1FF) == 0x1FF && (mlo + 1 == 0) && (ylo + man < man))
            return 0;
        xhi = mhi; xlo = mlo;
    }

    uint64_t msb = xhi >> 63;
    uint64_t ret_mant = xhi >> (msb + 9);
    ret_exp2 -= (1ULL ^ msb);

    /* Exact halfway case the algorithm cannot resolve -> defer. */
    if (xlo == 0 && (xhi & 0x1FF) == 0 && (ret_mant & 3) == 1) return 0;

    ret_mant += ret_mant & 1;
    ret_mant >>= 1;
    if ((ret_mant >> 53) > 0) { ret_mant >>= 1; ret_exp2 += 1; }

    /* Subnormal or overflow -> let the exact fallback decide. */
    if (ret_exp2 - 1 >= 0x7FF - 1) return 0;

    uint64_t bits = (ret_exp2 << 52) | (ret_mant & 0x000FFFFFFFFFFFFFULL);
    if (neg) bits |= 0x8000000000000000ULL;
    *out = nc_from_bits(bits);
    return 1;
}

/* ------------------------------------------------------------------ *
 * Layer 3: exact big-decimal fallback (decimal-to-binary, round-even).
 * The nc_decimal struct and its shift/round primitives live in decimal.h.
 * ------------------------------------------------------------------ */

static uint64_t dec_rounded_integer(const nc_decimal *a) {
    if (a->dp > 20) return 0xFFFFFFFFFFFFFFFFULL;
    int i;
    uint64_t n = 0;
    for (i = 0; i < a->dp && i < a->nd; i++)
        n = n * 10 + (uint64_t)(a->d[i] - '0');
    for (; i < a->dp; i++)
        n *= 10;
    if (nc_should_round_up(a, a->dp)) n++;
    return n;
}

static const int dec_powtab[] = {1, 3, 6, 9, 13, 16, 19, 23, 26};

/* Convert decimal -> binary64 bit pattern. Sets *overflow if it rounds to
 * infinity. Always returns a correctly-rounded result. */
static uint64_t dec_to_bits(nc_decimal *d, int *overflow) {
    int exp;
    uint64_t mant;
    int nonzero_in = d->nd != 0;
    *overflow = 0;

    if (d->nd == 0) { mant = 0; exp = NC_EXP_BIAS; goto out; }
    if (d->dp > 310) goto ovf;
    if (d->dp < -330) { mant = 0; exp = NC_EXP_BIAS; *overflow = 1; goto out; }

    exp = 0;
    while (d->dp > 0) {
        int n = (d->dp >= (int)(sizeof dec_powtab / sizeof dec_powtab[0]))
              ? 27 : dec_powtab[d->dp];
        nc_dec_shift(d, -n);
        exp += n;
    }
    while (d->dp < 0 || (d->dp == 0 && d->d[0] < '5')) {
        int n = (-d->dp >= (int)(sizeof dec_powtab / sizeof dec_powtab[0]))
              ? 27 : dec_powtab[-d->dp];
        nc_dec_shift(d, n);
        exp -= n;
    }

    exp--;  /* [0.5,1) -> [1,2) */

    if (exp < NC_EXP_BIAS + 1) {
        int n = NC_EXP_BIAS + 1 - exp;
        nc_dec_shift(d, -n);
        exp += n;
    }
    if (exp - NC_EXP_BIAS >= (1 << NC_EXP_BITS) - 1) goto ovf;

    nc_dec_shift(d, 1 + NC_MANT_BITS);
    mant = dec_rounded_integer(d);

    if (mant == ((uint64_t)2 << NC_MANT_BITS)) {
        mant >>= 1;
        exp++;
        if (exp - NC_EXP_BIAS >= (1 << NC_EXP_BITS) - 1) goto ovf;
    }
    if ((mant & ((uint64_t)1 << NC_MANT_BITS)) == 0) exp = NC_EXP_BIAS; /* subnormal */
    /* Nonzero input that rounded to zero is underflow (Go ErrRange). */
    if (mant == 0 && nonzero_in) *overflow = 1;
    goto out;

ovf:
    mant = 0;
    exp = (1 << NC_EXP_BITS) - 1 + NC_EXP_BIAS;
    *overflow = 1;

out: {
    uint64_t bits = mant & (((uint64_t)1 << NC_MANT_BITS) - 1);
    bits |= (uint64_t)((exp - NC_EXP_BIAS) & ((1 << NC_EXP_BITS) - 1)) << NC_MANT_BITS;
    if (d->neg) bits |= (uint64_t)1 << (NC_MANT_BITS + NC_EXP_BITS);
    return bits;
}
}

/* Parse the numeric literal [s, end) (digits, '.', exponent; no sign) into the
 * decimal struct. The literal is assumed already syntactically validated. */
static void dec_set(nc_decimal *d, const char *s, const char *end, int neg) {
    d->nd = 0; d->dp = 0; d->neg = neg; d->trunc = 0;
    int sawdot = 0;
    for (; s < end; s++) {
        char c = *s;
        if (c == '.') { sawdot = 1; d->dp = d->nd; continue; }
        if (c >= '0' && c <= '9') {
            if (c == '0' && d->nd == 0) { d->dp--; continue; }  /* leading zero */
            if (d->nd < NC_DEC_CAP) d->d[d->nd++] = (uint8_t)c;
            else if (c != '0') d->trunc = 1;
            continue;
        }
        break;  /* 'e'/'E' */
    }
    if (!sawdot) d->dp = d->nd;
    if (s < end && (*s == 'e' || *s == 'E')) {
        s++;
        int esign = 1;
        if (s < end && (*s == '+' || *s == '-')) { if (*s == '-') esign = -1; s++; }
        int e = 0;
        for (; s < end && *s >= '0' && *s <= '9'; s++)
            if (e < 10000) e = e * 10 + (*s - '0');
        d->dp += e * esign;
    }
    nc_dec_trim(d);
}

/* ------------------------------------------------------------------ *
 * Public entry point
 * ------------------------------------------------------------------ */
int neverc_strconv_parse_float(const char *s, double *result) {
    if (!s || !result)
        return NEVERC_STRCONV_ERR_SYNTAX;

    while (is_space(*s)) s++;
    if (*s == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    int sign = 1;
    if (*s == '+') s++;
    else if (*s == '-') { sign = -1; s++; }

    if ((s[0] == 'i' || s[0] == 'I') &&
        (s[1] == 'n' || s[1] == 'N') &&
        (s[2] == 'f' || s[2] == 'F')) {
        s += 3;
        if ((s[0] == 'i' || s[0] == 'I') &&
            (s[1] == 'n' || s[1] == 'N') &&
            (s[2] == 'i' || s[2] == 'I') &&
            (s[3] == 't' || s[3] == 'T') &&
            (s[4] == 'y' || s[4] == 'Y'))
            s += 5;
        while (is_space(*s)) s++;
        if (*s != '\0') return NEVERC_STRCONV_ERR_SYNTAX;
        *result = nc_make_inf(sign < 0);
        return NEVERC_STRCONV_OK;
    }
    if ((s[0] == 'n' || s[0] == 'N') &&
        (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'n' || s[2] == 'N')) {
        s += 3;
        while (is_space(*s)) s++;
        if (*s != '\0') return NEVERC_STRCONV_ERR_SYNTAX;
        *result = nc_make_nan();
        return NEVERC_STRCONV_OK;
    }

    if (!is_digit(*s) && *s != '.')
        return NEVERC_STRCONV_ERR_SYNTAX;

    /* ---- scan mantissa (capped at 19 significant digits) ---- */
    const char *lit = s;          /* start of numeric literal (for fallback) */
    uint64_t mantissa = 0;
    int nd = 0, nd_mant = 0, dp = 0;
    int trunc = 0, sawdot = 0, sawdigits = 0;

    for (;; s++) {
        char c = *s;
        if (c == '.') {
            if (sawdot) break;
            sawdot = 1; dp = nd;
            continue;
        }
        if (c >= '0' && c <= '9') {
            sawdigits = 1;
            if (c == '0' && nd == 0) { dp--; continue; }  /* leading zero */
            nd++;
            if (nd_mant < NC_MAX_MANT_DIGITS) {
                mantissa = mantissa * 10 + (uint64_t)(c - '0');
                nd_mant++;
            } else if (c != '0') {
                trunc = 1;
            }
            continue;
        }
        break;
    }
    if (!sawdigits)
        return NEVERC_STRCONV_ERR_SYNTAX;
    if (!sawdot) dp = nd;

    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '+') s++;
        else if (*s == '-') { exp_sign = -1; s++; }
        if (!is_digit(*s))
            return NEVERC_STRCONV_ERR_SYNTAX;
        int exp_val = 0;
        while (is_digit(*s)) {
            if (exp_val < 100000) exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        dp += exp_sign * exp_val;
    }

    const char *lit_end = s;      /* one past the numeric literal */

    while (is_space(*s)) s++;
    if (*s != '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    int neg = (sign < 0);

    if (mantissa == 0) {
        *result = neg ? -0.0 : 0.0;
        return NEVERC_STRCONV_OK;
    }

    int exp10 = dp - nd_mant;

    /* Layer 1: exact (only safe when no digits were dropped). */
    if (!trunc) {
        double f;
        if (atof64exact(mantissa, exp10, neg, &f)) { *result = f; return NEVERC_STRCONV_OK; }
    }

    /* Layer 2: Eisel-Lemire. */
    double f;
    int ok = eisel_lemire64(mantissa, exp10, neg, &f);
    if (ok) {
        if (!trunc) { *result = f; return NEVERC_STRCONV_OK; }
        /* Truncated input: accept only if mantissa+1 agrees (so the dropped
         * digits cannot change the result). */
        double f2;
        if (eisel_lemire64(mantissa + 1, exp10, neg, &f2)) {
            uint64_t b1, b2;
            memcpy(&b1, &f, 8); memcpy(&b2, &f2, 8);
            if (b1 == b2) { *result = f; return NEVERC_STRCONV_OK; }
        }
    }

    /* Layer 3: exact big-decimal fallback. */
    nc_decimal d = {0};
    dec_set(&d, lit, lit_end, neg);
    int overflow;
    uint64_t bits = dec_to_bits(&d, &overflow);
    *result = nc_from_bits(bits);
    if (overflow) return NEVERC_STRCONV_ERR_RANGE;
    return NEVERC_STRCONV_OK;
}
