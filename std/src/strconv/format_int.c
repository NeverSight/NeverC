#include "neverc/std/strconv.h"
#include <string.h>

static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

int neverc_strconv_format_uint(unsigned long long n, int base, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0 || base < 2 || base > 36)
        return -1;

    char tmp[65];
    int len = 0;

    if (n == 0) {
        tmp[len++] = '0';
    } else {
        while (n > 0) {
            tmp[len++] = digits[n % (unsigned)base];
            n /= (unsigned)base;
        }
    }

    if ((size_t)(len + 1) > bufsize)
        return -1;

    for (int i = 0; i < len; i++)
        buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

int neverc_strconv_format_int(long long n, int base, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0 || base < 2 || base > 36)
        return -1;

    int neg = 0;
    unsigned long long u;

    if (n < 0) {
        neg = 1;
        /* handle LLONG_MIN carefully */
        u = (unsigned long long)(-(n + 1)) + 1ULL;
    } else {
        u = (unsigned long long)n;
    }

    char tmp[66];
    int offset = 0;
    if (neg) {
        tmp[0] = '-';
        offset = 1;
    }

    int inner_len = neverc_strconv_format_uint(u, base, tmp + offset, sizeof(tmp) - (size_t)offset);
    if (inner_len < 0)
        return -1;

    int total = offset + inner_len;
    if ((size_t)(total + 1) > bufsize)
        return -1;

    memcpy(buf, tmp, (size_t)total);
    buf[total] = '\0';
    return total;
}

int neverc_strconv_itoa(int n, char *buf, size_t bufsize) {
    return neverc_strconv_format_int((long long)n, 10, buf, bufsize);
}

int neverc_strconv_ltoa(long long n, char *buf, size_t bufsize) {
    return neverc_strconv_format_int(n, 10, buf, bufsize);
}
