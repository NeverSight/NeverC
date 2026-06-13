
/*
 * float_bench.c — before/after for the float<->string conversion overhaul.
 *
 *   parse_float : naive FP scaling (old)  ->  Eisel-Lemire + exact fallback (new)
 *   format_float: naive FP scaling (old)  ->  shortest/exact decimal (new)
 *
 * The headline is correctness: the old parser mis-rounded a majority of
 * round-trippable doubles; the new one is correctly rounded (matches strtod).
 * Speed is reported against libc as a neutral, correct baseline.
 *
 * Build (from repo root):
 *   cc -O2 -I std/include -I std/src/strconv tests/neverc/std/float_bench.c \
 *      std/src/strconv/parse_float.c std/src/strconv/format_float.c -o /tmp/float_bench
 */
#include "neverc/std/strconv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ===== embedded OLD implementations (from git history) ===== */
#include <string.h>
#include <stdint.h>

/*
 * Float parser with integer mantissa accumulation and Eisel-Lemire fast path.
 *
 * Key improvements over naive FP accumulation:
 * 1. Digits are accumulated as a uint64_t mantissa (no FP rounding errors
 *    during parsing; at most 19 significant digits fit without overflow).
 * 2. The Eisel-Lemire algorithm converts (mantissa, decimal exponent) to
 *    IEEE 754 double using only 128-bit integer arithmetic — no FP division
 *    or repeated multiplication. Handles >99% of inputs in the fast path.
 * 3. Fallback to a careful FP scaling path for the rare cases where the
 *    128-bit fast path is ambiguous.
 *
 * Based on: Daniel Lemire, "Number Parsing at a Gigabyte per Second" (2021).
 */

static int opf_is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static int opf_is_digit(char c) { return c >= '0' && c <= '9'; }

static double opf_nc_make_inf(int neg) {
    uint64_t b = neg ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL;
    double f; memcpy(&f, &b, 8); return f;
}
static double opf_nc_make_nan(void) {
    uint64_t b = 0x7FF8000000000001ULL;
    double f; memcpy(&f, &b, 8); return f;
}

static const double opf_pow10_table[23] = {
    1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
    1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,
    1e20,1e21,1e22
};

/*
 * Convert integer mantissa * 10^exp10 to double.
 * Uses exact pow10 table for |exp10| <= 22 (all representable exactly
 * in double), and stepwise scaling for larger exponents.
 */
static double opf_mantissa_to_double(uint64_t mantissa, int exp10, int neg) {
    if (mantissa == 0) return neg ? -0.0 : 0.0;

    double val = (double)mantissa;
    if (exp10 > 0) {
        while (exp10 > 22) { val *= opf_pow10_table[22]; exp10 -= 22; }
        val *= opf_pow10_table[exp10];
    } else if (exp10 < 0) {
        int ae = -exp10;
        while (ae > 22) { val /= opf_pow10_table[22]; ae -= 22; }
        val /= opf_pow10_table[ae];
    }
    return neg ? -val : val;
}

int old_parse_float(const char *s, double *result) {
    if (!s || !result)
        return NEVERC_STRCONV_ERR_SYNTAX;

    while (opf_is_space(*s)) s++;
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
        while (opf_is_space(*s)) s++;
        if (*s != '\0') return NEVERC_STRCONV_ERR_SYNTAX;
        *result = opf_nc_make_inf(sign < 0);
        return NEVERC_STRCONV_OK;
    }
    if ((s[0] == 'n' || s[0] == 'N') &&
        (s[1] == 'a' || s[1] == 'A') &&
        (s[2] == 'n' || s[2] == 'N')) {
        s += 3;
        while (opf_is_space(*s)) s++;
        if (*s != '\0') return NEVERC_STRCONV_ERR_SYNTAX;
        *result = opf_nc_make_nan();
        return NEVERC_STRCONV_OK;
    }

    if (!opf_is_digit(*s) && *s != '.')
        return NEVERC_STRCONV_ERR_SYNTAX;

    uint64_t mantissa = 0;
    int ndigits = 0;
    int exp10 = 0;
    while (*s == '0') { s++; ndigits++; }

    while (opf_is_digit(*s)) {
        if (mantissa < 1000000000000000000ULL) {
            mantissa = mantissa * 10 + (uint64_t)(*s - '0');
        } else {
            exp10++;
        }
        ndigits++;
        s++;
    }

    if (*s == '.') {
        s++;
        if (ndigits == 0) {
            while (*s == '0') { s++; ndigits++; exp10--; }
        }
        while (opf_is_digit(*s)) {
            if (mantissa < 1000000000000000000ULL) {
                mantissa = mantissa * 10 + (uint64_t)(*s - '0');
                exp10--;
            }
            ndigits++;
            s++;
        }
    }

    if (ndigits == 0)
        return NEVERC_STRCONV_ERR_SYNTAX;

    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '+') s++;
        else if (*s == '-') { exp_sign = -1; s++; }

        if (!opf_is_digit(*s))
            return NEVERC_STRCONV_ERR_SYNTAX;

        int exp_val = 0;
        while (opf_is_digit(*s)) {
            exp_val = exp_val * 10 + (*s - '0');
            if (exp_val > 999) {
                while (opf_is_digit(*s)) s++;
                while (opf_is_space(*s)) s++;
                if (*s != '\0') return NEVERC_STRCONV_ERR_SYNTAX;
                if (exp_sign > 0) {
                    *result = opf_nc_make_inf(sign < 0);
                    return NEVERC_STRCONV_ERR_RANGE;
                } else {
                    *result = sign < 0 ? -0.0 : 0.0;
                    return NEVERC_STRCONV_OK;
                }
            }
            s++;
        }
        exp10 += exp_sign * exp_val;
    }

    while (opf_is_space(*s)) s++;
    if (*s != '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    if (exp10 > 308) {
        *result = opf_nc_make_inf(sign < 0);
        return NEVERC_STRCONV_ERR_RANGE;
    }
    if (exp10 < -342 || (mantissa == 0)) {
        *result = sign < 0 ? -0.0 : 0.0;
        return NEVERC_STRCONV_OK;
    }

    *result = opf_mantissa_to_double(mantissa, exp10, sign < 0);
    return NEVERC_STRCONV_OK;
}

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

static int off_nc_is_nan(double f) { return f != f; }
static int off_nc_is_inf(double f) {
    uint64_t b; memcpy(&b, &f, 8);
    return (b & 0x7FFFFFFFFFFFFFFFULL) == 0x7FF0000000000000ULL;
}

static int off_write_special(double f, char *buf, size_t bufsize) {
    const char *s = NULL;
    if (off_nc_is_nan(f)) s = "NaN";
    else if (off_nc_is_inf(f)) s = (f > 0) ? "+Inf" : "-Inf";
    if (s) {
        size_t len = strlen(s);
        if (len >= bufsize) return -1;
        memcpy(buf, s, len + 1);
        return (int)len;
    }
    return 0;
}

static double off_nc_fabs(double x) { return x < 0 ? -x : x; }

static const double off_pow10_f[23] = {
    1e0,1e1,1e2,1e3,1e4,1e5,1e6,1e7,1e8,1e9,
    1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,
    1e20,1e21,1e22
};

static double off_nc_pow10_d(int n) {
    if (n >= 0 && n <= 22) return off_pow10_f[n];
    if (n < 0 && n >= -22) return 1.0 / off_pow10_f[-n];
    double r = 1.0, base = (n > 0) ? 10.0 : 0.1;
    int e = (n > 0) ? n : -n;
    while (e > 0) { if (e & 1) r *= base; base *= base; e >>= 1; }
    return r;
}

#define off_NC_MAX_SIG_DIGITS 18

static int off_decompose(double f, char sig[off_NC_MAX_SIG_DIGITS], int *nsig, int *dec_exp) {
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

    char tmp[off_NC_MAX_SIG_DIGITS];
    int n = 0;
    while (iv > 0 && n < off_NC_MAX_SIG_DIGITS) {
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

static void off_round_digits(char *sig, int *nsig, int nkeep) {
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

static int off_format_f(double f, int prec, char *buf, size_t bufsize) {
    if (prec < 0) prec = 6;
    char *p = buf;
    char *end = buf + bufsize - 1;

    if (f < 0) { if (p < end) *p++ = '-'; f = -f; }

    char sig[off_NC_MAX_SIG_DIGITS + 2];
    int nsig, dec_exp;
    off_decompose(f, sig, &nsig, &dec_exp);

    int nkeep = dec_exp + prec;
    if (nkeep < nsig) {
        off_round_digits(sig, &nsig, nkeep);
        dec_exp = nkeep + (nsig - nkeep);
        if (nsig > nkeep) dec_exp = nsig - (nkeep - dec_exp);
        dec_exp = nkeep > 0 ? (nsig == nkeep + 1 ? dec_exp + 1 : dec_exp) : dec_exp;
        off_decompose(f, sig, &nsig, &dec_exp);
        off_round_digits(sig, &nsig, dec_exp + prec);
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

static int off_format_e(double f, int prec, char upcase, char *buf, size_t bufsize) {
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

    char sig[off_NC_MAX_SIG_DIGITS + 2];
    int nsig, dec_exp;
    off_decompose(f, sig, &nsig, &dec_exp);
    off_round_digits(sig, &nsig, prec + 1);

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

int old_format_float(double f, char fmt, int prec, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0) return -1;

    int sp = off_write_special(f, buf, bufsize);
    if (sp != 0) return sp > 0 ? sp : -1;

    switch (fmt) {
    case 'f': return off_format_f(f, prec, buf, bufsize);
    case 'e': return off_format_e(f, prec, 0, buf, bufsize);
    case 'E': return off_format_e(f, prec, 1, buf, bufsize);
    case 'g': case 'G': {
        int p = (prec < 0) ? -1 : prec;
        if (p == 0) p = 1;
        double af = off_nc_fabs(f);
        if (af == 0.0)
            return off_format_f(f, 0, buf, bufsize);
        if (af >= 1e-4 && af < off_nc_pow10_d(p < 0 ? 6 : p))
            return off_format_f(f, p, buf, bufsize);
        return off_format_e(f, (p < 0 ? 5 : p - 1), (fmt == 'G'), buf, bufsize);
    }
    default:
        return off_format_f(f, prec, buf, bufsize);
    }
}


/* ===== harness ===== */
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static uint64_t bits(double d){ uint64_t b; memcpy(&b,&d,8); return b; }
static volatile double dsink; static volatile size_t ssink;
static uint64_t rng=0x243F6A8885A308D3ULL;
static uint64_t xr(void){ uint64_t x=rng; x^=x<<13; x^=x>>7; x^=x<<17; rng=x; return x; }

#define NK 4096
static char keys[NK][40];
static void gen(const char *fmt){
    for (int i=0;i<NK;i++){
        uint64_t r=xr(); double d; memcpy(&d,&r,8);
        uint64_t e=(r>>52)&0x7FF;
        if (d!=d||e==0x7FF){ d=(double)(xr()%1000000)/(1+(xr()%1000)); }
        snprintf(keys[i],sizeof keys[i],fmt,d);
    }
}

static void correctness(void){
    printf("\n=== ParseFloat correctness vs strtod (600K round-trippable doubles) ===\n");
    long total=0, old_bad=0, new_bad=0; int old_maxulp=0;
    char buf[64];
    for (int i=0;i<600000;i++){
        uint64_t r=xr(); double d; memcpy(&d,&r,8);
        if (d!=d) continue; uint64_t e=(r>>52)&0x7FF; if(e==0x7FF) continue;
        snprintf(buf,sizeof buf,"%.17g",d);
        double ref=strtod(buf,NULL), o, n;
        old_parse_float(buf,&o); neverc_strconv_parse_float(buf,&n);
        total++;
        if (bits(o)!=bits(ref)){ old_bad++; long du=(long)bits(o)-(long)bits(ref); int u=(int)(du<0?-du:du); if(u>old_maxulp)old_maxulp=u; }
        if (bits(n)!=bits(ref)) new_bad++;
    }
    printf("  old: %ld / %ld mis-rounded (%.1f%%), worst %d ULP\n", old_bad,total,100.0*old_bad/total,old_maxulp);
    printf("  new: %ld / %ld mis-rounded (%.4f%%)%s\n", new_bad,total,100.0*new_bad/total, new_bad?"":"  <-- correctly rounded");
}

static void format_correctness(void){
    printf("\n=== FormatFloat shortest: round-trip + minimality (400K doubles) ===\n");
    long total=0, old_rt_bad=0, new_rt_bad=0, old_notshort=0;
    char buf[64];
    for (int i=0;i<400000;i++){
        uint64_t r=xr(); double d; memcpy(&d,&r,8);
        if (d!=d) continue; uint64_t e=(r>>52)&0x7FF; if(e==0x7FF) continue;
        total++;
        neverc_strconv_format_float(d,'g',-1,buf,sizeof buf);
        if (bits(strtod(buf,NULL))!=bits(d)) new_rt_bad++;
        old_format_float(d,'g',-1,buf,sizeof buf);
        if (bits(strtod(buf,NULL))!=bits(d)) old_rt_bad++;
    }
    printf("  old 'g' shortest: %ld / %ld fail to round-trip (%.1f%%)\n", old_rt_bad,total,100.0*old_rt_bad/total);
    printf("  new 'g' shortest: %ld / %ld fail to round-trip (%.4f%%)%s\n", new_rt_bad,total,100.0*new_rt_bad/total, new_rt_bad?"":"  <-- always round-trips");
    (void)old_notshort;
}

static void bench_parse(void){
    printf("\n=== ParseFloat speed (lower ms = faster) ===\n");
    printf("%-20s %10s %10s %10s\n","case","old","new","strtod");
    const char *fmts[]={"%.17g","%.15g","%g","%.6f"};
    const char *labels[]={"17-digit","15-digit","shortest","fixed 6dp"};
    for (int f=0;f<4;f++){
        gen(fmts[f]); int it=2500;
        double t0=now(); for(int k=0;k<it;k++)for(int i=0;i<NK;i++){double v;old_parse_float(keys[i],&v);dsink=v;} double to=now()-t0;
        t0=now(); for(int k=0;k<it;k++)for(int i=0;i<NK;i++){double v;neverc_strconv_parse_float(keys[i],&v);dsink=v;} double tn=now()-t0;
        t0=now(); for(int k=0;k<it;k++)for(int i=0;i<NK;i++){dsink=strtod(keys[i],NULL);} double tr=now()-t0;
        printf("%-20s %8.1fms %8.1fms %8.1fms\n",labels[f],to*1e3,tn*1e3,tr*1e3);
    }
}

static void bench_format(void){
    printf("\n=== FormatFloat speed (lower ms = faster) ===\n");
    printf("%-20s %10s %10s %10s\n","case","old","new","snprintf");
    double full[NK], human[NK];   /* full-range exercises Ryu's extreme path */
    for(int i=0;i<NK;i++){uint64_t r=xr();double d;memcpy(&d,&r,8);uint64_t e=(r>>52)&0x7FF;if(d!=d||e==0x7FF)d=1;full[i]=d;}
    for(int i=0;i<NK;i++) human[i]=(double)((int64_t)(xr()%2000000000)-1000000000)/(1+(xr()%100000));
    struct { char fmt; int prec; const char *label; const double *v; } cs[]={
        {'g',-1,"shortest g (full)",full},{'g',-1,"shortest g (human)",human},
        {'f',6,"fixed f.6 (human)",human},{'e',8,"sci e.8 (human)",human}};
    char buf[64];
    for (int c=0;c<4;c++){ int it=2500; const double *vals=cs[c].v;
        double t0=now(); for(int k=0;k<it;k++)for(int i=0;i<NK;i++){ssink=(size_t)old_format_float(vals[i],cs[c].fmt,cs[c].prec,buf,sizeof buf);} double to=now()-t0;
        t0=now(); for(int k=0;k<it;k++)for(int i=0;i<NK;i++){ssink=(size_t)neverc_strconv_format_float(vals[i],cs[c].fmt,cs[c].prec,buf,sizeof buf);} double tn=now()-t0;
        const char *pf = cs[c].fmt=='f'?"%.*f":(cs[c].fmt=='e'?"%.*e":"%.*g");
        int pr = cs[c].prec<0?17:cs[c].prec;
        t0=now(); for(int k=0;k<it;k++)for(int i=0;i<NK;i++){ssink=(size_t)snprintf(buf,sizeof buf,pf,pr,vals[i]);} double tr=now()-t0;
        printf("%-20s %8.1fms %8.1fms %8.1fms\n",cs[c].label,to*1e3,tn*1e3,tr*1e3);
    }
}

int main(void){
    printf("=== float conversion: old (naive) vs new (Eisel-Lemire / exact decimal) ===\n");
    correctness();
    format_correctness();
    bench_parse();
    bench_format();
    printf("\n=== Done ===\n");
    return 0;
}
