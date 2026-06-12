#include "neverc/std/strconv.h"
#include <string.h>

static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";

static const char digit_pairs[201] =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

int neverc_strconv_format_uint(unsigned long long n, int base, char *buf, size_t bufsize) {
    if (!buf || bufsize == 0 || base < 2 || base > 36)
        return -1;

    if (n == 0) {
        if (bufsize < 2) return -1;
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    char tmp[65];
    int pos = 65;

    if (base == 10) {
        while (n >= 100) {
            unsigned idx = (unsigned)(n % 100) * 2;
            n /= 100;
            tmp[--pos] = digit_pairs[idx + 1];
            tmp[--pos] = digit_pairs[idx];
        }
        if (n >= 10) {
            unsigned idx = (unsigned)n * 2;
            tmp[--pos] = digit_pairs[idx + 1];
            tmp[--pos] = digit_pairs[idx];
        } else {
            tmp[--pos] = '0' + (char)n;
        }
    } else if ((base & (base - 1)) == 0) {
        int shift = __builtin_ctz(base);
        unsigned mask = (unsigned)base - 1;
        while (n > 0) {
            tmp[--pos] = digits[n & mask];
            n >>= shift;
        }
    } else {
        while (n > 0) {
            tmp[--pos] = digits[n % (unsigned)base];
            n /= (unsigned)base;
        }
    }

    int len = 65 - pos;
    if ((size_t)(len + 1) > bufsize)
        return -1;

    memcpy(buf, tmp + pos, (size_t)len);
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
