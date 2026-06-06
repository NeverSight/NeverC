#include "neverc/strconv.h"
#include <string.h>
#include <stdint.h>

/*
 * Self-implemented double-to-string formatting.
 * Supports 'e', 'E', 'f', 'g', 'G' formats.
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

static double nc_fabs(double x) { return x < 0 ? -x : x; }

static double nc_pow10_d(int n) {
    static const double p[] = {1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,
                               1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18};
    if (n >= 0 && n <= 18) return p[n];
    if (n < 0 && n >= -18) return 1.0/p[-n];
    double r = 1.0, base = (n>0) ? 10.0 : 0.1;
    int e = (n>0) ? n : -n;
    while (e > 0) { if (e&1) r *= base; base *= base; e >>= 1; }
    return r;
}

static int format_f(double f, int prec, char *buf, size_t bufsize) {
    if (prec < 0) prec = 6;
    char *p = buf;
    char *end = buf + bufsize - 1;

    if (f < 0) { if (p < end) *p++ = '-'; f = -f; }

    /* Find the number of integer digits */
    int ndigits = 0;
    {
        double t = f;
        if (t < 1.0) ndigits = 1;
        else { while (t >= 1.0) { t *= 0.1; ndigits++; } }
    }

    /* Extract integer digits from most-significant to least-significant.
       This avoids casting to uint64_t which overflows for f >= 2^64. */
    double rem = f;
    for (int i = ndigits - 1; i >= 0; i--) {
        double scale = nc_pow10_d(i);
        int d = (int)(rem / scale);
        if (d > 9) d = 9;
        if (d < 0) d = 0;
        if (p < end) *p++ = '0' + d;
        rem -= d * scale;
    }

    if (prec > 0) {
        if (p < end) *p++ = '.';
        double frac = rem;
        for (int i = 0; i < prec && p < end; i++) {
            frac *= 10.0;
            int d = (int)frac;
            if (d > 9) d = 9;
            *p++ = '0' + d;
            frac -= d;
        }
    }
    *p = '\0';
    return (int)(p - buf);
}

static int format_e(double f, int prec, char upcase, char *buf, size_t bufsize) {
    if (prec < 0) prec = 6;
    char *p = buf;
    char *end = buf + bufsize - 1;

    if (f < 0) { if (p < end) *p++ = '-'; f = -f; }
    if (f == 0.0) {
        if (p < end) *p++ = '0';
        if (prec > 0) {
            if (p < end) *p++ = '.';
            for (int i = 0; i < prec && p < end; i++) *p++ = '0';
        }
        if (p < end) *p++ = upcase ? 'E' : 'e';
        if (p < end) *p++ = '+';
        if (p < end) *p++ = '0';
        if (p < end) *p++ = '0';
        *p = '\0';
        return (int)(p - buf);
    }

    int exp10 = 0;
    while (f >= 10.0) { f /= 10.0; exp10++; }
    while (f < 1.0 && f > 0.0) { f *= 10.0; exp10--; }

    /* Round */
    f += 0.5 * nc_pow10_d(-prec);
    if (f >= 10.0) { f /= 10.0; exp10++; }

    int d = (int)f;
    if (p < end) *p++ = '0' + d;
    f -= d;

    if (prec > 0) {
        if (p < end) *p++ = '.';
        for (int i = 0; i < prec && p < end; i++) {
            f *= 10.0;
            d = (int)f;
            if (d > 9) d = 9;
            *p++ = '0' + d;
            f -= d;
        }
    }

    if (p < end) *p++ = upcase ? 'E' : 'e';
    if (exp10 < 0) { if (p < end) *p++ = '-'; exp10 = -exp10; }
    else { if (p < end) *p++ = '+'; }

    if (exp10 >= 100) {
        if (p < end) *p++ = '0' + exp10/100;
        if (p < end) *p++ = '0' + (exp10/10)%10;
        if (p < end) *p++ = '0' + exp10%10;
    } else {
        if (p < end) *p++ = '0' + exp10/10;
        if (p < end) *p++ = '0' + exp10%10;
    }
    *p = '\0';
    return (int)(p - buf);
}

int neverc_strconv_format_float(double f, char fmt, int prec, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) return -1;

    int sp = write_special(f, buf, bufsize);
    if (sp != 0) return sp > 0 ? sp : -1;

    switch (fmt) {
    case 'f': return format_f(f, prec, buf, bufsize);
    case 'e': return format_e(f, prec, 0, buf, bufsize);
    case 'E': return format_e(f, prec, 1, buf, bufsize);
    case 'g': case 'G': {
        int p = (prec < 0) ? -1 : prec;
        if (p == 0) p = 1;
        double af = nc_fabs(f);
        if (af == 0.0)
            return format_f(f, 0, buf, bufsize);
        if (af >= 1e-4 && af < nc_pow10_d(p < 0 ? 6 : p))
            return format_f(f, p, buf, bufsize);
        return format_e(f, (p < 0 ? 5 : p - 1), (fmt == 'G'), buf, bufsize);
    }
    default:
        return format_f(f, prec, buf, bufsize);
    }
}
