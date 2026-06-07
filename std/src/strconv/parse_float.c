#include "neverc/strconv.h"
#include <string.h>
#include <stdint.h>

/*
 * Self-implemented floating-point string parser.
 * Handles: [+-]digits[.digits][eE[+-]digits], "inf", "nan"
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

static double nc_pow10(int n) {
    static const double p10[] = {
        1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9,
        1e10,1e11,1e12,1e13,1e14,1e15,1e16,1e17,1e18,1e19,
        1e20,1e21,1e22
    };
    if (n >= 0 && n <= 22) return p10[n];
    if (n >= -22 && n < 0) return 1.0 / p10[-n];

    double result = 1.0;
    double base = (n > 0) ? 10.0 : 0.1;
    int e = (n > 0) ? n : -n;
    while (e > 0) {
        if (e & 1) result *= base;
        base *= base;
        e >>= 1;
    }
    return result;
}

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
        /* Accept "infinity" (case-insensitive) like Go's ParseFloat */
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

    double integer_part = 0.0;
    while (is_digit(*s)) {
        integer_part = integer_part * 10.0 + (*s - '0');
        s++;
    }

    double frac_part = 0.0;
    if (*s == '.') {
        s++;
        double frac_scale = 0.1;
        while (is_digit(*s)) {
            frac_part += (*s - '0') * frac_scale;
            frac_scale *= 0.1;
            s++;
        }
    }

    double val = integer_part + frac_part;

    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '+') s++;
        else if (*s == '-') { exp_sign = -1; s++; }

        if (!is_digit(*s))
            return NEVERC_STRCONV_ERR_SYNTAX;

        int exp_val = 0;
        while (is_digit(*s)) {
            exp_val = exp_val * 10 + (*s - '0');
            if (exp_val > 400) {
                *result = (exp_sign > 0) ? nc_make_inf(sign < 0) : 0.0;
                return NEVERC_STRCONV_ERR_RANGE;
            }
            s++;
        }
        val *= nc_pow10(exp_sign * exp_val);
    }

    while (is_space(*s)) s++;
    if (*s != '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    *result = sign * val;
    return NEVERC_STRCONV_OK;
}
