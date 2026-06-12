#include "neverc/std/strconv.h"
#include <string.h>
#include <stdint.h>

/*
 * Double-to-string formatting using integer digit extraction.
 *
 * Key improvement: extract significant digits into a uint64_t buffer
 * using a single FP→integer conversion, then format from the integer
 * digits. This avoids the cascading rounding errors of repeated
 * FP multiply/divide used in the old implementation.
 *
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

static const double pow10_f[23] = {
    1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,
    1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,
    1e20,1e21,1e22
};

static double nc_pow10_d(int n) {
    if (n >= 0 && n <= 22) return pow10_f[n];
    if (n < 0 && n >= -22) return 1.0 / pow10_f[-n];
    double r = 1.0, base = (n > 0) ? 10.0 : 0.1;
    int e = (n > 0) ? n : -n;
    while (e > 0) { if (e & 1) r *= base; base *= base; e >>= 1; }
    return r;
}

#define NC_MAX_SIG_DIGITS 18

static int decompose(double f, char sig[NC_MAX_SIG_DIGITS], int *nsig, int *dec_exp) {
    if (f == 0.0) {
        sig[0] = '0';
        *nsig = 1;
        *dec_exp = 0;
        return 0;
    }

    int exp10 = 0;
    if (f >= 1e18)  { while (f >= 1e18)  { f *= 1e-1;  exp10++; } }
    else if (f >= 1e9)  { /* within range */ }
    else if (f >= 1.0)  { /* fine */ }
    else { while (f < 1e-1 && f > 0) { f *= 1e1; exp10--; } }

    while (f >= 1e18) { f *= 0.1; exp10++; }
    while (f < 1e17 && f > 0.0) { f *= 10.0; exp10--; }

    uint64_t iv = (uint64_t)(f + 0.5);
    if (iv >= 1000000000000000000ULL) {
        iv /= 10;
        exp10++;
    }

    char tmp[NC_MAX_SIG_DIGITS];
    int n = 0;
    while (iv > 0 && n < NC_MAX_SIG_DIGITS) {
        tmp[n++] = '0' + (char)(iv % 10);
        iv /= 10;
    }
    if (n == 0) { tmp[n++] = '0'; }

    for (int i = 0; i < n; i++)
        sig[i] = tmp[n - 1 - i];
    *nsig = n;
    *dec_exp = exp10 + n;
    return 0;
}

static void round_digits(char *sig, int *nsig, int nkeep) {
    if (nkeep >= *nsig) return;
    if (nkeep < 0) nkeep = 0;
    int carry = (nkeep < *nsig && sig[nkeep] >= '5') ? 1 : 0;
    *nsig = nkeep;
    while (carry && nkeep > 0) {
        nkeep--;
        sig[nkeep]++;
        if (sig[nkeep] <= '9') { carry = 0; break; }
        sig[nkeep] = '0';
    }
    if (carry) {
        for (int i = *nsig; i > 0; i--) sig[i] = sig[i - 1];
        sig[0] = '1';
        (*nsig)++;
    }
    while (*nsig > 1 && sig[*nsig - 1] == '0')
        (*nsig)--;
}

static int format_f(double f, int prec, char *buf, size_t bufsize) {
    if (prec < 0) prec = 6;
    char *p = buf;
    char *end = buf + bufsize - 1;

    if (f < 0) { if (p < end) *p++ = '-'; f = -f; }

    char sig[NC_MAX_SIG_DIGITS + 2];
    int nsig, dec_exp;
    decompose(f, sig, &nsig, &dec_exp);

    int nkeep = dec_exp + prec;
    if (nkeep < nsig) {
        round_digits(sig, &nsig, nkeep);
        dec_exp = nkeep + (nsig - nkeep);
        if (nsig > nkeep) dec_exp = nsig - (nkeep - dec_exp);
        dec_exp = nkeep > 0 ? (nsig == nkeep + 1 ? dec_exp + 1 : dec_exp) : dec_exp;
        decompose(f, sig, &nsig, &dec_exp);
        round_digits(sig, &nsig, dec_exp + prec);
    }

    if (dec_exp <= 0) {
        if (p < end) *p++ = '0';
    } else {
        for (int i = 0; i < dec_exp; i++) {
            if (p >= end) break;
            *p++ = (i < nsig) ? sig[i] : '0';
        }
    }

    if (prec > 0) {
        if (p < end) *p++ = '.';
        for (int i = 0; i < prec; i++) {
            if (p >= end) break;
            int idx = dec_exp + i;
            if (idx < 0)
                *p++ = '0';
            else if (idx < nsig)
                *p++ = sig[idx];
            else
                *p++ = '0';
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

    char sig[NC_MAX_SIG_DIGITS + 2];
    int nsig, dec_exp;
    decompose(f, sig, &nsig, &dec_exp);
    round_digits(sig, &nsig, prec + 1);

    int exp_out = dec_exp - 1;
    if (nsig > 0 && nsig > prec + 1) {
        exp_out += nsig - (prec + 1);
    }

    if (p < end) *p++ = sig[0];
    if (prec > 0) {
        if (p < end) *p++ = '.';
        for (int i = 0; i < prec && p < end; i++)
            *p++ = (i + 1 < nsig) ? sig[i + 1] : '0';
    }

    if (p < end) *p++ = upcase ? 'E' : 'e';
    if (exp_out < 0) { if (p < end) *p++ = '-'; exp_out = -exp_out; }
    else { if (p < end) *p++ = '+'; }

    if (exp_out >= 100) {
        if (p < end) *p++ = '0' + exp_out / 100;
        if (p < end) *p++ = '0' + (exp_out / 10) % 10;
        if (p < end) *p++ = '0' + exp_out % 10;
    } else {
        if (p < end) *p++ = '0' + exp_out / 10;
        if (p < end) *p++ = '0' + exp_out % 10;
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
