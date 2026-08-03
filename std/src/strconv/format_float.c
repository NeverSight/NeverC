#include "neverc/std/strconv.h"
#include "decimal.h"
#include "ryu_table.h"
#include <string.h>
#include <stdint.h>

/*
 * Double-to-string formatting via exact decimal conversion.
 *
 * The double is decomposed into its exact value (mantissa x 2^exp) and that
 * value is materialized as an arbitrary-precision decimal (decimal.h). From
 * there:
 *   - prec < 0 ("shortest"): round to the fewest digits that still parse back
 *     to the same double (Steele & White / Grisu-free, as in Go's strconv).
 *   - prec >= 0: round to the requested number of digits.
 * Both are correctly rounded (round half to even).
 *
 * The previous implementation extracted digits by naive FP scaling, which was
 * neither shortest (0.1 printed as "0.100000") nor reliably correctly rounded.
 * Supports 'e', 'E', 'f', 'g', 'G'.
 */

static int nc_is_nan(double f) { return f != f; }
static int nc_is_inf(double f) {
    uint64_t b; memcpy(&b, &f, 8);
    return (b & 0x7FFFFFFFFFFFFFFFULL) == 0x7FF0000000000000ULL;
}

static int write_special(double f, char *buf, size_t bufsize) {
    const char *s = NULL;
    if (nc_is_nan(f)) s = "NaN";
    else if (nc_is_inf(f)) s = (f > 0) ? "+Inf" : "-Inf";
    if (s) {
        size_t len = strlen(s);
        if (len >= bufsize) return -1;
        memcpy(buf, s, len + 1);
        return (int)len;
    }
    return 0;
}

static int nc_min_i(int a, int b) { return a < b ? a : b; }
static int nc_max_i(int a, int b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ *
 * decimal helpers specific to formatting (assign + rounding)
 * ------------------------------------------------------------------ */
static void dec_assign(nc_decimal *a, uint64_t v) {
    char buf[24];
    int n = 0;
    while (v > 0) { uint64_t v1 = v / 10; buf[n++] = (char)('0' + (v - 10 * v1)); v = v1; }
    a->nd = 0;
    for (n--; n >= 0; n--) a->d[a->nd++] = (uint8_t)buf[n];
    a->dp = a->nd;
    a->neg = 0;
    a->trunc = 0;
    nc_dec_trim(a);
}

static void dec_round_down(nc_decimal *a, int nd) {
    if (nd < 0 || nd >= a->nd) return;
    a->nd = nd;
    nc_dec_trim(a);
}

static void dec_round_up(nc_decimal *a, int nd) {
    if (nd < 0 || nd >= a->nd) return;
    for (int i = nd - 1; i >= 0; i--) {
        if (a->d[i] < '9') { a->d[i]++; a->nd = i + 1; return; }
    }
    /* all nines: 999.. -> 1000.. */
    a->d[0] = '1';
    a->nd = 1;
    a->dp++;
}

static void dec_round(nc_decimal *a, int nd) {
    if (nd < 0 || nd >= a->nd) return;
    if (nc_should_round_up(a, nd)) dec_round_up(a, nd);
    else dec_round_down(a, nd);
}

/* Fill d with the decimal value mantissa * 10^e10 (trailing zeros trimmed). */
static void dec_from_mant_exp(nc_decimal *d, uint64_t mant, int e10) {
    if (mant == 0) { d->nd = 0; d->dp = 0; d->trunc = 0; return; }
    char tmp[20];
    int n = 0;
    while (mant > 0) { tmp[n++] = (char)('0' + (mant % 10)); mant /= 10; }
    for (int i = 0; i < n; i++) d->d[i] = (uint8_t)tmp[n - 1 - i];
    d->nd = n;
    d->dp = n + e10;
    d->trunc = 0;
    nc_dec_trim(d);
}

/* ------------------------------------------------------------------ *
 * Ryu: shortest correctly-rounded decimal (Ulf Adams, 2018).
 * Fast for every double (no slow path), unlike the exact-decimal route.
 * ------------------------------------------------------------------ */
#define NCI_RYU_BIAS 1023

static inline uint64_t ryu_umul128(uint64_t a, uint64_t b, uint64_t *hi) {
#ifdef __SIZEOF_INT128__
    __uint128_t r = (__uint128_t)a * b; *hi = (uint64_t)(r >> 64); return (uint64_t)r;
#else
    uint64_t ah = a >> 32, al = (uint32_t)a, bh = b >> 32, bl = (uint32_t)b;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    *hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return (ll & 0xffffffffULL) | (mid << 32);
#endif
}
static inline uint64_t ryu_shr128(uint64_t lo, uint64_t hi, uint32_t dist) {
    return (hi << (64 - dist)) | (lo >> dist);  /* 0 < dist < 64 */
}
static uint64_t ryu_mulshift(uint64_t m, const uint64_t *mul, int32_t j) {
    uint64_t hi1, lo1 = ryu_umul128(m, mul[1], &hi1);
    uint64_t hi0; ryu_umul128(m, mul[0], &hi0);
    uint64_t sum = hi0 + lo1;
    if (sum < hi0) hi1++;
    return ryu_shr128(sum, hi1, (uint32_t)(j - 64));
}
static uint64_t ryu_mulshift_all(uint64_t m, const uint64_t *mul, int32_t j,
                                 uint64_t *vp, uint64_t *vm, uint32_t mmShift) {
    *vp = ryu_mulshift(4 * m + 2, mul, j);
    *vm = ryu_mulshift(4 * m - 1 - mmShift, mul, j);
    return ryu_mulshift(4 * m, mul, j);
}
static inline int32_t ryu_log10pow2(int32_t e) { return (int32_t)(((uint32_t)e * 78913) >> 18); }
static inline int32_t ryu_log10pow5(int32_t e) { return (int32_t)(((uint32_t)e * 732923) >> 20); }
static inline uint32_t ryu_pow5bits(int32_t e) { return (uint32_t)((((uint32_t)e * 1217359) >> 19) + 1); }
static inline uint32_t ryu_pow5factor(uint64_t v) {
    const uint64_t m_inv_5 = 14757395258967641293ULL;
    const uint64_t n_div_5 = 3689348814741910323ULL;
    uint32_t count = 0;
    for (;;) { v *= m_inv_5; if (v > n_div_5) break; count++; }
    return count;
}
static inline int ryu_mult_pow5(uint64_t v, uint32_t p) { return ryu_pow5factor(v) >= p; }
static inline int ryu_mult_pow2(uint64_t v, uint32_t p) { return (v & (((uint64_t)1 << p) - 1)) == 0; }

/* Shortest decimal for a finite nonzero double: value = *out_mant * 10^*out_exp. */
static void ryu_shortest(uint64_t ieee_mant, uint32_t ieee_exp,
                         uint64_t *out_mant, int32_t *out_exp) {
    int32_t e2;
    uint64_t m2;
    if (ieee_exp == 0) {
        e2 = 1 - NCI_RYU_BIAS - NC_MANT_BITS - 2;
        m2 = ieee_mant;
    } else {
        e2 = (int32_t)ieee_exp - NCI_RYU_BIAS - NC_MANT_BITS - 2;
        m2 = ((uint64_t)1 << NC_MANT_BITS) | ieee_mant;
    }
    int even = (m2 & 1) == 0;
    int accept = even;
    uint64_t mv = 4 * m2;
    uint32_t mmShift = (ieee_mant != 0 || ieee_exp <= 1);

    uint64_t vr, vp, vm;
    int32_t e10;
    int vmTZ = 0, vrTZ = 0;
    if (e2 >= 0) {
        uint32_t q = (uint32_t)ryu_log10pow2(e2) - (e2 > 3);
        e10 = (int32_t)q;
        int32_t k = NCI_RYU_POW5_INV_BITCOUNT + (int32_t)ryu_pow5bits((int32_t)q) - 1;
        int32_t i = -e2 + (int32_t)q + k;
        vr = ryu_mulshift_all(m2, nci_ryu_pow5_inv[q], i, &vp, &vm, mmShift);
        if (q <= 21) {
            if (mv % 5 == 0) vrTZ = ryu_mult_pow5(mv, q);
            else if (accept) vmTZ = ryu_mult_pow5(mv - 1 - mmShift, q);
            else vp -= (uint64_t)ryu_mult_pow5(mv + 2, q);
        }
    } else {
        uint32_t q = (uint32_t)ryu_log10pow5(-e2) - (-e2 > 1);
        e10 = (int32_t)q + e2;
        int32_t i = -e2 - (int32_t)q;
        int32_t k = (int32_t)ryu_pow5bits(i) - NCI_RYU_POW5_BITCOUNT;
        int32_t j = (int32_t)q - k;
        vr = ryu_mulshift_all(m2, nci_ryu_pow5[i], j, &vp, &vm, mmShift);
        if (q <= 1) {
            vrTZ = 1;
            if (accept) vmTZ = (mmShift == 1);
            else --vp;
        } else if (q < 63) {
            vrTZ = ryu_mult_pow2(mv, q);
        }
    }

    int32_t removed = 0;
    uint8_t lastDigit = 0;
    uint64_t output;
    if (vmTZ || vrTZ) {
        for (;;) {
            uint64_t vpd = vp / 10, vmd = vm / 10;
            if (vpd <= vmd) break;
            uint32_t vmmod = (uint32_t)vm - 10 * (uint32_t)vmd;
            uint64_t vrd = vr / 10;
            uint32_t vrmod = (uint32_t)vr - 10 * (uint32_t)vrd;
            vmTZ &= (vmmod == 0);
            vrTZ &= (lastDigit == 0);
            lastDigit = (uint8_t)vrmod;
            vr = vrd; vp = vpd; vm = vmd; ++removed;
        }
        if (vmTZ) {
            for (;;) {
                uint64_t vmd = vm / 10;
                uint32_t vmmod = (uint32_t)vm - 10 * (uint32_t)vmd;
                if (vmmod != 0) break;
                uint64_t vpd = vp / 10, vrd = vr / 10;
                uint32_t vrmod = (uint32_t)vr - 10 * (uint32_t)vrd;
                vrTZ &= (lastDigit == 0);
                lastDigit = (uint8_t)vrmod;
                vr = vrd; vp = vpd; vm = vmd; ++removed;
            }
        }
        if (vrTZ && lastDigit == 5 && vr % 2 == 0) lastDigit = 4;
        output = vr + ((vr == vm && (!accept || !vmTZ)) || lastDigit >= 5);
    } else {
        int roundUp = 0;
        uint64_t vpd = vp / 100, vmd = vm / 100;
        if (vpd > vmd) {
            uint64_t vrd = vr / 100;
            uint32_t vrmod = (uint32_t)vr - 100 * (uint32_t)vrd;
            roundUp = vrmod >= 50;
            vr = vrd; vp = vpd; vm = vmd; removed += 2;
        }
        for (;;) {
            uint64_t vpd2 = vp / 10, vmd2 = vm / 10;
            if (vpd2 <= vmd2) break;
            uint64_t vrd = vr / 10;
            uint32_t vrmod = (uint32_t)vr - 10 * (uint32_t)vrd;
            roundUp = vrmod >= 5;
            vr = vrd; vp = vpd2; vm = vmd2; ++removed;
        }
        output = vr + (vr == vm || roundUp);
    }
    *out_mant = output;
    *out_exp = e10 + removed;
}

/* ------------------------------------------------------------------ *
 * digit-slice -> formatted text (bounds-checked writer)
 * ------------------------------------------------------------------ */
typedef struct { char *p; char *end; int of; } nc_w;
static void w_ch(nc_w *w, char c) { if (w->p < w->end) *w->p++ = c; else w->of = 1; }
static void w_pad(nc_w *w, char c, int n) { while (n-- > 0) w_ch(w, c); }
static void w_digs(nc_w *w, const uint8_t *d, int from, int to) {
    for (int i = from; i < to; i++) w_ch(w, (char)d[i]);
}

static void fmt_e(nc_w *w, int neg, const nc_decimal *d, int prec, char ech) {
    if (neg) w_ch(w, '-');
    w_ch(w, d->nd != 0 ? (char)d->d[0] : '0');
    if (prec > 0) {
        w_ch(w, '.');
        int i = 1;
        int m = nc_min_i(d->nd, prec + 1);
        if (i < m) { w_digs(w, d->d, i, m); i = m; }
        for (; i <= prec; i++) w_ch(w, '0');
    }
    w_ch(w, ech);
    int exp = d->dp - 1;
    if (d->nd == 0) exp = 0;
    if (exp < 0) { w_ch(w, '-'); exp = -exp; }
    else w_ch(w, '+');
    if (exp < 10)       { w_ch(w, '0'); w_ch(w, (char)('0' + exp)); }
    else if (exp < 100) { w_ch(w, (char)('0' + exp / 10)); w_ch(w, (char)('0' + exp % 10)); }
    else                { w_ch(w, (char)('0' + exp / 100)); w_ch(w, (char)('0' + (exp / 10) % 10)); w_ch(w, (char)('0' + exp % 10)); }
}

static void fmt_f(nc_w *w, int neg, const nc_decimal *d, int prec) {
    if (neg) w_ch(w, '-');
    if (d->dp > 0) {
        int m = nc_min_i(d->nd, d->dp);
        w_digs(w, d->d, 0, m);
        w_pad(w, '0', d->dp - m);
    } else {
        w_ch(w, '0');
    }
    if (prec > 0) {
        w_ch(w, '.');
        for (int i = 0; i < prec; i++) {
            int j = d->dp + i;
            w_ch(w, (j >= 0 && j < d->nd) ? (char)d->d[j] : '0');
        }
    }
}

/* fmt is 'e','E','f','g','G'. shortest controls the 'g' precision heuristic. */
static int format_digits(char *buf, size_t bufsize, int shortest, int neg,
                         const nc_decimal *d, int prec, char fmt) {
    nc_w w = { buf, buf + bufsize - 1, 0 };
    switch (fmt) {
    case 'e': case 'E':
        fmt_e(&w, neg, d, prec, fmt);
        break;
    case 'f':
        fmt_f(&w, neg, d, prec);
        break;
    case 'g': case 'G': {
        int eprec = prec;
        if (eprec > d->nd && d->nd >= d->dp) eprec = d->nd;
        if (shortest) eprec = 6;
        int exp = d->dp - 1;
        if (exp < -4 || exp >= eprec) {
            if (prec > d->nd) prec = d->nd;
            fmt_e(&w, neg, d, prec - 1, (char)(fmt + 'e' - 'g'));
        } else {
            if (prec > d->dp) prec = d->nd;
            fmt_f(&w, neg, d, nc_max_i(prec - d->dp, 0));
        }
        break;
    }
    default:
        fmt_f(&w, neg, d, prec);
        break;
    }
    if (w.of) return -1;
    *w.p = '\0';
    return (int)(w.p - buf);
}

int neverc_strconv_format_float(double f, char fmt, int prec, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) return -1;

    int sp = write_special(f, buf, bufsize);
    if (sp != 0) return sp > 0 ? sp : -1;

    uint64_t bits; memcpy(&bits, &f, 8);
    int neg = (int)(bits >> (NC_EXP_BITS + NC_MANT_BITS));
    uint32_t ieee_exp = (uint32_t)((bits >> NC_MANT_BITS) & ((1u << NC_EXP_BITS) - 1));
    uint64_t ieee_mant = bits & (((uint64_t)1 << NC_MANT_BITS) - 1);

    nc_decimal d = {0};
    d.neg = neg;

    int shortest = (prec < 0);
    if (shortest) {
        /* Ryu: fast shortest correctly-rounded digits for every double. */
        if (ieee_exp == 0 && ieee_mant == 0) {
            d.nd = 0; d.dp = 0; d.trunc = 0;       /* zero */
        } else {
            uint64_t rmant; int32_t rexp10;
            ryu_shortest(ieee_mant, ieee_exp, &rmant, &rexp10);
            dec_from_mant_exp(&d, rmant, rexp10);
        }
        switch (fmt) {
        case 'e': case 'E': prec = nc_max_i(d.nd - 1, 0); break;
        case 'f':           prec = nc_max_i(d.nd - d.dp, 0); break;
        case 'g': case 'G': prec = d.nd; break;
        default:            prec = nc_max_i(d.nd - 1, 0); break;
        }
    } else {
        /* Fixed precision: exact decimal value, then round. */
        int rexp = (int)ieee_exp;
        uint64_t mant = ieee_mant;
        if (rexp == 0) rexp++;                          /* denormal */
        else mant |= (uint64_t)1 << NC_MANT_BITS;       /* implicit bit */
        int exp = rexp + NC_EXP_BIAS;
        dec_assign(&d, mant);
        d.neg = neg;
        nc_dec_shift(&d, exp - NC_MANT_BITS);
        switch (fmt) {
        case 'e': case 'E': dec_round(&d, prec + 1); break;
        case 'f':           dec_round(&d, d.dp + prec); break;
        case 'g': case 'G':
            if (prec == 0) prec = 1;
            dec_round(&d, prec);
            break;
        default:            dec_round(&d, prec + 1); break;
        }
    }

    return format_digits(buf, bufsize, shortest, neg, &d, prec, fmt);
}
