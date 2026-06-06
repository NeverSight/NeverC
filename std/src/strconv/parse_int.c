#include "neverc/strconv.h"
#include <limits.h>
#include <string.h>
#include <ctype.h>

int neverc_strconv_parse_uint(const char *s, int base, unsigned long long *result) {
    if (!s || !result || *s == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;
    if (base != 0 && (base < 2 || base > 36))
        return NEVERC_STRCONV_ERR_BASE;

    const char *p = s;

    if (base == 0) {
        if (p[0] == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                base = 16;
                p += 2;
            } else if (p[1] == 'b' || p[1] == 'B') {
                base = 2;
                p += 2;
            } else if (p[1] == 'o' || p[1] == 'O') {
                base = 8;
                p += 2;
            } else if (p[1] >= '0' && p[1] <= '7') {
                base = 8;
                p += 1;
            } else {
                base = 10;
            }
        } else {
            base = 10;
        }
    }

    if (*p == '\0')
        return NEVERC_STRCONV_ERR_SYNTAX;

    unsigned long long cutoff = ULLONG_MAX / (unsigned long long)base;
    unsigned long long val = 0;
    int any = 0;

    for (; *p; p++) {
        if (*p == '_' && any)
            continue;

        int digit;
        if (*p >= '0' && *p <= '9')
            digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z')
            digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z')
            digit = *p - 'A' + 10;
        else
            return NEVERC_STRCONV_ERR_SYNTAX;

        if (digit >= base)
            return NEVERC_STRCONV_ERR_SYNTAX;

        if (val > cutoff || (val == cutoff && (unsigned long long)digit > ULLONG_MAX % (unsigned long long)base)) {
            *result = ULLONG_MAX;
            return NEVERC_STRCONV_ERR_RANGE;
        }
        val = val * (unsigned long long)base + (unsigned long long)digit;
        any = 1;
    }

    if (!any)
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
        *result = 0;
        return rc;
    }

    if (neg) {
        if (uval > (unsigned long long)LLONG_MAX + 1ULL) {
            *result = LLONG_MIN;
            return NEVERC_STRCONV_ERR_RANGE;
        }
        *result = -(long long)uval;
    } else {
        if (uval > (unsigned long long)LLONG_MAX) {
            *result = LLONG_MAX;
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
        if (result) *result = 0;
        return rc;
    }
    if (val < INT_MIN || val > INT_MAX) {
        if (result) *result = val < 0 ? INT_MIN : INT_MAX;
        return NEVERC_STRCONV_ERR_RANGE;
    }
    if (result) *result = (int)val;
    return NEVERC_STRCONV_OK;
}

int neverc_strconv_atol(const char *s, long long *result) {
    return neverc_strconv_parse_int(s, 10, result);
}
