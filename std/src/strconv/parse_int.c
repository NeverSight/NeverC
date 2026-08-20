#include "neverc/std/strconv.h"
#include <string.h>
#include <stdint.h>

#define NC_ULLONG_MAX  18446744073709551615ULL
#define NC_LLONG_MAX   9223372036854775807LL
#define NC_LLONG_MIN   (-9223372036854775807LL - 1)
#define NC_INT_MAX     2147483647
#define NC_INT_MIN     (-2147483647 - 1)

/*
 * digit_val[c] maps an ASCII byte to its numeric value (0-35) for bases up to
 * 36, or 0xFF for any non-alphanumeric byte. A single table lookup followed by
 * one unsigned compare ("d >= base") replaces the old three-way range branch
 * and the separate "digit >= base" test: 0xFF is rejected for every base 2-36,
 * and '_' (0xFF here) still falls through to the separator/syntax handling.
 */
#define NC_X 0xFF
static const uint8_t digit_val[256] = {
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
       0,   1,   2,   3,   4,   5,   6,   7,    8,   9,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,  10,  11,  12,  13,  14,  15,  16,   17,  18,  19,  20,  21,  22,  23,  24,
      25,  26,  27,  28,  29,  30,  31,  32,   33,  34,  35,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,  10,  11,  12,  13,  14,  15,  16,   17,  18,  19,  20,  21,  22,  23,  24,
      25,  26,  27,  28,  29,  30,  31,  32,   33,  34,  35,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
    NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X, NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,NC_X,
};
#undef NC_X

int neverc_strconv_parse_uint(const char *s, int base, unsigned long long *result) {
    if (!s || !result || *s == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;
    if (base != 0 && (base < 2 || base > 36))
        return NEVERC_STRCONV_ERR_BASE;

    const char *p = s;
    int allow_underscores = base == 0;
    int saw_digit = 0;
    int previous_underscore = 0;
    int after_prefix = 0;

    if (base == 0) {
        if (p[0] == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                base = 16;
                p += 2;
                after_prefix = 1;
            } else if (p[1] == 'b' || p[1] == 'B') {
                base = 2;
                p += 2;
                after_prefix = 1;
            } else if (p[1] == 'o' || p[1] == 'O') {
                base = 8;
                p += 2;
                after_prefix = 1;
            } else {
                base = 8;
                p += 1;
                saw_digit = 1; /* The leading zero is a digit and a prefix. */
                after_prefix = 1;
            }
        } else {
            base = 10;
        }
    }

    if (*p == '\0' && !saw_digit)
        return NEVERC_STRCONV_ERR_SYNTAX;

    unsigned long long val = 0;

    if (base == 10) {
        /*
         * Fast path for base 10 (covers Atoi and the vast majority of calls).
         * The cutoff/remainder are now compile-time constants and the digit
         * test "(unsigned)(c-'0') > 9" needs a single comparison.
         */
        const unsigned long long cutoff = NC_ULLONG_MAX / 10ULL;       /* 1844674407370955161 */
        const unsigned rem = (unsigned)(NC_ULLONG_MAX % 10ULL);        /* 5 */
        for (; *p; p++) {
            unsigned d = (unsigned)(unsigned char)*p - '0';
            if (d > 9) {
                if (*p == '_' && allow_underscores &&
                    !previous_underscore && (saw_digit || after_prefix)) {
                    previous_underscore = 1;
                    after_prefix = 0;
                    continue;
                }
                return NEVERC_STRCONV_ERR_SYNTAX;
            }
            /* Go ParseUint returns ErrRange immediately on overflow and
             * never inspects later bytes (including junk or '_'). */
            if (val > cutoff || (val == cutoff && d > rem)) {
                *result = NC_ULLONG_MAX;
                return NEVERC_STRCONV_ERR_RANGE;
            }
            val = val * 10ULL + d;
            saw_digit = 1;
            previous_underscore = 0;
            after_prefix = 0;
        }
    } else {
        const unsigned long long ubase = (unsigned long long)base;
        const unsigned long long cutoff = NC_ULLONG_MAX / ubase;
        const unsigned rem = (unsigned)(NC_ULLONG_MAX % ubase);
        for (; *p; p++) {
            unsigned char c = (unsigned char)*p;
            if (c == '_' && allow_underscores &&
                !previous_underscore && (saw_digit || after_prefix)) {
                previous_underscore = 1;
                after_prefix = 0;
                continue;
            }

            unsigned d = digit_val[c];
            if (d >= (unsigned)base)
                return NEVERC_STRCONV_ERR_SYNTAX;

            if (val > cutoff || (val == cutoff && d > rem)) {
                *result = NC_ULLONG_MAX;
                return NEVERC_STRCONV_ERR_RANGE;
            }
            val = val * ubase + d;
            saw_digit = 1;
            previous_underscore = 0;
            after_prefix = 0;
        }
    }

    if (!saw_digit || previous_underscore)
        return NEVERC_STRCONV_ERR_SYNTAX;

    *result = val;
    return NEVERC_STRCONV_OK;
}

int neverc_strconv_parse_int(const char *s, int base, long long *result) {
    if (!s || !result || *s == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    const char *p = s;
    int neg = 0;
    if (*p == '+') {
        p++;
    } else if (*p == '-') {
        neg = 1;
        p++;
    }

    unsigned long long uval;
    int rc = neverc_strconv_parse_uint(p, base, &uval);
    if (rc != NEVERC_STRCONV_OK) {
        if (rc == NEVERC_STRCONV_ERR_RANGE)
            *result = neg ? NC_LLONG_MIN : NC_LLONG_MAX;
        else
            *result = 0;
        return rc;
    }

    if (neg) {
        if (uval > (unsigned long long)NC_LLONG_MAX + 1ULL) {
            *result = NC_LLONG_MIN;
            return NEVERC_STRCONV_ERR_RANGE;
        }
        /* Negate in unsigned space: -(long long)uval would overflow (UB) for
         * uval == LLONG_MAX+1; unsigned wraparound + cast is well-defined and
         * yields the identical value on two's-complement targets. */
        *result = (long long)(0ULL - uval);
    } else {
        if (uval > (unsigned long long)NC_LLONG_MAX) {
            *result = NC_LLONG_MAX;
            return NEVERC_STRCONV_ERR_RANGE;
        }
        *result = (long long)uval;
    }
    return NEVERC_STRCONV_OK;
}

int neverc_strconv_atoi(const char *s, int *result) {
    long long val;
    int rc = neverc_strconv_parse_int(s, 10, &val);
    if (rc != NEVERC_STRCONV_OK) {
        if (result) {
            if (rc == NEVERC_STRCONV_ERR_RANGE)
                *result = val < 0 ? NC_INT_MIN : NC_INT_MAX;
            else
                *result = 0;
        }
        return rc;
    }
    if (val < NC_INT_MIN || val > NC_INT_MAX) {
        if (result) *result = val < 0 ? NC_INT_MIN : NC_INT_MAX;
        return NEVERC_STRCONV_ERR_RANGE;
    }
    if (result) *result = (int)val;
    return NEVERC_STRCONV_OK;
}

int neverc_strconv_atol(const char *s, long long *result) {
    return neverc_strconv_parse_int(s, 10, result);
}
