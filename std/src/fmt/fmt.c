#include "neverc/std/fmt.h"
#include "neverc/std/strconv.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

/* Dynamic buffer for building formatted strings */
typedef struct {
    char  *data;
    size_t len;
    size_t cap;
    int failed;
} fmtbuf_t;

static void buf_init(fmtbuf_t *b) {
    b->cap = 128;
    b->data = (char *)malloc(b->cap);
    b->len = 0;
    b->failed = b->data == NULL;
    if (b->failed) b->cap = 0;
}

static int buf_grow(fmtbuf_t *b, size_t need) {
    if (b->failed || b->len == SIZE_MAX ||
        need > SIZE_MAX - b->len - 1) {
        b->failed = 1;
        return 0;
    }
    size_t required = b->len + need + 1;
    if (required <= b->cap) return 1;
    size_t next = b->cap < 128 ? 128 : b->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2) {
            next = required;
            break;
        }
        next *= 2;
    }
    char *grown = (char *)realloc(b->data, next);
    if (!grown) {
        b->failed = 1;
        return 0;
    }
    b->data = grown;
    b->cap = next;
    return 1;
}

static void buf_putc(fmtbuf_t *b, char c) {
    if (!buf_grow(b, 1)) return;
    b->data[b->len++] = c;
}

static void buf_puts(fmtbuf_t *b, const char *s, size_t n) {
    /* One capacity check for the whole run (vs. one per byte through buf_putc),
     * then a copy the compiler lowers to memcpy/vector stores. The single grow
     * check is what makes copying literal runs and verb output cheap. */
    if (!buf_grow(b, n)) return;
    char *d = b->data + b->len;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    b->len += n;
}

static void buf_pad(fmtbuf_t *b, char c, int count) {
    for (int i = 0; i < count && !b->failed; i++) buf_putc(b, c);
}

static size_t my_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int append_position(const char *buf, size_t cap, size_t *position) {
    if (!buf || cap == 0) return 0;
    size_t n = 0;
    while (n < cap && buf[n]) n++;
    if (n == cap) return 0;
    *position = n;
    return 1;
}

/* Two-digit lookup table for base-10 conversion: emit two decimal digits per
 * 64-bit divide instead of one per byte. */
static const char fmt_digit_pairs[201] =
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

/* Integer to string conversion */
#if defined(__GNUC__) || defined(__clang__)
#define NCI_FMT_INLINE static inline __attribute__((always_inline))
#else
#define NCI_FMT_INLINE static inline
#endif

/* Base 10 (the overwhelmingly common case): two digits per divide via the pair
 * table. Kept separate from the general path so neither carries a base switch. */
NCI_FMT_INLINE int fmt_uint10(char *buf, uint64_t val) {
    if (val == 0) { buf[0] = '0'; return 1; }
    char tmp[24];
    int pos = 0;
    while (val >= 100) {
        unsigned idx = (unsigned)(val % 100) * 2;
        val /= 100;
        tmp[pos++] = fmt_digit_pairs[idx + 1];
        tmp[pos++] = fmt_digit_pairs[idx];
    }
    if (val >= 10) {
        unsigned idx = (unsigned)val * 2;
        tmp[pos++] = fmt_digit_pairs[idx + 1];
        tmp[pos++] = fmt_digit_pairs[idx];
    } else {
        tmp[pos++] = (char)('0' + val);
    }
    int wi = 0;
    for (int i = pos - 1; i >= 0; i--) buf[wi++] = tmp[i];
    return wi;
}

NCI_FMT_INLINE int fmt_int10(char *buf, int64_t val) {
    if (val < 0) {
        buf[0] = '-';
        /* Negate in unsigned space: -(int64)val overflows (UB) for INT64_MIN;
         * 0 - (uint64)val wraps to the correct magnitude on two's complement. */
        return 1 + fmt_uint10(buf + 1, 0ULL - (uint64_t)val);
    }
    return fmt_uint10(buf, (uint64_t)val);
}

/* Non-decimal bases (x/X/o/b/p): unchanged one-digit-per-divide path. */
static int fmt_uint_base(char *buf, uint64_t val, int base, int uppercase) {
    if (val == 0) { buf[0] = '0'; return 1; }
    const char *digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[72];
    int pos = 0;
    while (val > 0) {
        tmp[pos++] = digits[val % (unsigned)base];
        val /= (unsigned)base;
    }
    int wi = 0;
    for (int i = pos - 1; i >= 0; i--) buf[wi++] = tmp[i];
    return wi;
}

/* IEEE 754 double decomposition */
static uint64_t f64_to_bits(double x) {
    union { double d; uint64_t u; } u;
    u.d = x;
    return u.u;
}

static int f64_isnan(double x) { return x != x; }

/* Float formatting delegates to strconv's correctly-rounded engine
 * (Ryu shortest + exact decimal), shared so fmt and strconv stay consistent. */
static int fmt_float_f(char *buf, size_t cap, double val, int prec) {
    if (prec < 0) prec = 6;
    int n = neverc_strconv_format_float(val, 'f', prec, buf, cap);
    return n < 0 ? 0 : n;
}
static int fmt_float_e(char *buf, size_t cap, double val, int prec, int uppercase) {
    if (prec < 0) prec = 6;
    int n = neverc_strconv_format_float(val, uppercase ? 'E' : 'e', prec, buf, cap);
    return n < 0 ? 0 : n;
}
/* prec < 0 => shortest round-trippable form (Go's %v / %g default). */
static int fmt_float_g(char *buf, size_t cap, double val, int prec, int uppercase) {
    int n = neverc_strconv_format_float(val, uppercase ? 'G' : 'g', prec, buf, cap);
    return n < 0 ? 0 : n;
}

/* Core formatting engine */
char *neverc_fmt_vsprintf(const char *format, va_list args) {
    if (!format) return NULL;
    fmtbuf_t buf;
    buf_init(&buf);

    size_t flen = my_strlen(format);
    for (size_t i = 0; i < flen; i++) {
        if (format[i] != '%') {
            /* Copy the literal run up to the next '%' in one shot instead of one
             * char (and one capacity check) at a time. A single-char run (the
             * common separator between verbs) takes the cheap buf_putc path with
             * no memchr/memcpy call overhead; longer runs locate the next '%'
             * with memchr and bulk-copy. */
            size_t start = i;
            i++;
            if (i < flen && format[i] != '%') {
                const char *pct = (const char *)memchr(format + i, '%', flen - i);
                i = pct ? (size_t)(pct - format) : flen;
            }
            size_t runlen = i - start;
            if (runlen == 1) buf_putc(&buf, format[start]);
            else buf_puts(&buf, format + start, runlen);
            i--;   /* loop's i++ lands on '%' or one past the end */
            continue;
        }
        i++;
        if (i >= flen) break;

        /* Parse flags */
        int flag_minus = 0, flag_plus = 0, flag_zero = 0, flag_space = 0, flag_hash = 0;
        while (i < flen) {
            if      (format[i] == '-') { flag_minus = 1; i++; }
            else if (format[i] == '+') { flag_plus  = 1; i++; }
            else if (format[i] == '0') { flag_zero  = 1; i++; }
            else if (format[i] == ' ') { flag_space = 1; i++; }
            else if (format[i] == '#') { flag_hash  = 1; i++; }
            else break;
        }
        (void)flag_hash;

        /* Parse width */
        int width = 0;
        int has_width = 0;
        if (i < flen && format[i] == '*') {
            width = va_arg(args, int);
            has_width = 1;
            i++;
        } else {
            while (i < flen && format[i] >= '0' && format[i] <= '9') {
                int digit = format[i] - '0';
                if (width > (INT_MAX - digit) / 10) goto format_fail;
                width = width * 10 + digit;
                has_width = 1;
                i++;
            }
        }

        /* Parse precision */
        int prec = -1;
        if (i < flen && format[i] == '.') {
            i++;
            prec = 0;
            if (i < flen && format[i] == '*') {
                prec = va_arg(args, int);
                i++;
            } else {
                while (i < flen && format[i] >= '0' && format[i] <= '9') {
                    int digit = format[i] - '0';
                    if (prec > (INT_MAX - digit) / 10) goto format_fail;
                    prec = prec * 10 + digit;
                    i++;
                }
            }
        }

        /* Parse length modifier */
        int is_long = 0, is_longlong = 0;
        if (i < flen && format[i] == 'l') {
            is_long = 1; i++;
            if (i < flen && format[i] == 'l') { is_longlong = 1; i++; }
        }
        (void)is_long;

        if (i >= flen) break;
        char verb = format[i];
        char tmp[512];   /* large enough for %f of values up to ~1e308 */
        int tlen = 0;
        int is_negative = 0;

        switch (verb) {
        case '%':
            buf_putc(&buf, '%');
            continue;
        case 'd': case 'i': {
            int64_t val = is_longlong ? va_arg(args, long long) :
                          is_long ? (int64_t)va_arg(args, long) :
                          (int64_t)va_arg(args, int);
            is_negative = val < 0;
            tlen = fmt_int10(tmp, val);
            break;
        }
        case 'u': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) :
                           is_long ? (uint64_t)va_arg(args, unsigned long) :
                           (uint64_t)va_arg(args, unsigned int);
            tlen = fmt_uint10(tmp, val);
            break;
        }
        case 'x': case 'X': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) :
                           is_long ? (uint64_t)va_arg(args, unsigned long) :
                           (uint64_t)va_arg(args, unsigned int);
            tlen = fmt_uint_base(tmp, val, 16, verb == 'X');
            break;
        }
        case 'o': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) :
                           is_long ? (uint64_t)va_arg(args, unsigned long) :
                           (uint64_t)va_arg(args, unsigned int);
            tlen = fmt_uint_base(tmp, val, 8, 0);
            break;
        }
        case 'b': {
            uint64_t val = is_longlong ? va_arg(args, unsigned long long) :
                           is_long ? (uint64_t)va_arg(args, unsigned long) :
                           (uint64_t)va_arg(args, unsigned int);
            tlen = fmt_uint_base(tmp, val, 2, 0);
            break;
        }
        case 'c': {
            int ch = va_arg(args, int);
            tmp[0] = (char)ch;
            tlen = 1;
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            size_t slen = my_strlen(s);
            if (prec >= 0 && (size_t)prec < slen) slen = (size_t)prec;
            int pad = (has_width && width > 0 && (size_t)width > slen)
                          ? width - (int)slen : 0;
            if (!flag_minus) buf_pad(&buf, ' ', pad);
            buf_puts(&buf, s, slen);
            if (flag_minus) buf_pad(&buf, ' ', pad);
            continue;
        }
        case 'f': {
            double val = va_arg(args, double);
            tlen = fmt_float_f(tmp, sizeof tmp, val, prec);
            is_negative = (f64_to_bits(val) >> 63) && !f64_isnan(val);
            break;
        }
        case 'e': case 'E': {
            double val = va_arg(args, double);
            tlen = fmt_float_e(tmp, sizeof tmp, val, prec, verb == 'E');
            break;
        }
        case 'g': case 'G': {
            double val = va_arg(args, double);
            tlen = fmt_float_g(tmp, sizeof tmp, val, prec, verb == 'G');
            break;
        }
        case 'p': {
            void *ptr = va_arg(args, void *);
            tmp[0] = '0'; tmp[1] = 'x';
            tlen = 2 + fmt_uint_base(tmp + 2, (uint64_t)(uintptr_t)ptr, 16, 0);
            break;
        }
        default:
            buf_putc(&buf, '%');
            buf_putc(&buf, verb);
            continue;
        }

        /* Apply width/padding */
        int prefix_len = 0;
        if (flag_plus && !is_negative && (verb == 'd' || verb == 'i' || verb == 'f'))
            prefix_len = 1;
        else if (flag_space && !is_negative && (verb == 'd' || verb == 'i'))
            prefix_len = 1;

        int total = tlen + prefix_len;
        int pad = (has_width && width > total) ? width - total : 0;

        if (!flag_minus && !flag_zero) buf_pad(&buf, ' ', pad);
        if (flag_plus && !is_negative && (verb == 'd' || verb == 'i' || verb == 'f'))
            buf_putc(&buf, '+');
        else if (flag_space && !is_negative && (verb == 'd' || verb == 'i'))
            buf_putc(&buf, ' ');
        if (!flag_minus && flag_zero) buf_pad(&buf, '0', pad);
        buf_puts(&buf, tmp, tlen);
        if (flag_minus) buf_pad(&buf, ' ', pad);
    }

    buf_putc(&buf, '\0');
    if (buf.failed) goto format_fail;
    return buf.data;

format_fail:
    free(buf.data);
    return NULL;
}

char *neverc_fmt_sprintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *result = neverc_fmt_vsprintf(format, args);
    va_end(args);
    return result;
}

int neverc_fmt_fprintf(FILE *f, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *s = neverc_fmt_vsprintf(format, args);
    va_end(args);
    if (!s) return -1;
    size_t len = my_strlen(s);
    fwrite(s, 1, len, f);
    free(s);
    return (int)len;
}

int neverc_fmt_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *s = neverc_fmt_vsprintf(format, args);
    va_end(args);
    if (!s) return -1;
    size_t len = my_strlen(s);
    fwrite(s, 1, len, stdout);
    free(s);
    return (int)len;
}

int neverc_fmt_println(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *s = neverc_fmt_vsprintf(format, args);
    va_end(args);
    if (!s) return -1;
    size_t len = my_strlen(s);
    fwrite(s, 1, len, stdout);
    fwrite("\n", 1, 1, stdout);
    free(s);
    return (int)len + 1;
}

char *neverc_fmt_sprint(const char *s) {
    if (!s) s = "";
    size_t len = my_strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i++) out[i] = s[i];
    out[len] = '\0';
    return out;
}

char *neverc_fmt_sprintln(const char *s) {
    if (!s) s = "";
    size_t len = my_strlen(s);
    char *out = (char *)malloc(len + 2);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i++) out[i] = s[i];
    out[len] = '\n';
    out[len + 1] = '\0';
    return out;
}

int neverc_fmt_fprint(FILE *f, const char *s) {
    if (!s || !f) return -1;
    size_t len = my_strlen(s);
    fwrite(s, 1, len, f);
    return (int)len;
}

int neverc_fmt_fprintln(FILE *f, const char *s) {
    if (!s || !f) return -1;
    size_t len = my_strlen(s);
    fwrite(s, 1, len, f);
    fwrite("\n", 1, 1, f);
    return (int)len + 1;
}

char *neverc_fmt_errorf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *s = neverc_fmt_vsprintf(format, args);
    va_end(args);
    return s;
}

/* ---- Scan functions ---- */

static int skip_ws(const char **p) {
    int n = 0;
    while (**p == ' ' || **p == '\t' || **p == '\n' || **p == '\r') { (*p)++; n++; }
    return n;
}

static int scan_int(const char **p, int64_t *out) {
    skip_ws(p);
    if (**p == '\0') return 0;
    int neg = 0;
    if (**p == '-') { neg = 1; (*p)++; }
    else if (**p == '+') { (*p)++; }
    if (**p < '0' || **p > '9') return 0;
    int64_t val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    *out = neg ? -val : val;
    return 1;
}

static int scan_uint(const char **p, uint64_t *out) {
    skip_ws(p);
    if (**p < '0' || **p > '9') return 0;
    uint64_t val = 0;
    while (**p >= '0' && **p <= '9') {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    *out = val;
    return 1;
}

static int scan_float(const char **p, double *out) {
    skip_ws(p);
    const char *start = *p;
    if (**p == '-' || **p == '+') (*p)++;
    if ((**p < '0' || **p > '9') && **p != '.') { *p = start; return 0; }

    /* Delimit the numeric token, then hand it to strconv's correctly-rounded
     * parser instead of accumulating in floating point. */
    while (**p >= '0' && **p <= '9') (*p)++;
    if (**p == '.') { (*p)++; while (**p >= '0' && **p <= '9') (*p)++; }
    if (**p == 'e' || **p == 'E') {
        const char *esave = *p;
        (*p)++;
        if (**p == '-' || **p == '+') (*p)++;
        if (**p >= '0' && **p <= '9') { while (**p >= '0' && **p <= '9') (*p)++; }
        else *p = esave;            /* lone 'e' is not part of the number */
    }

    size_t len = (size_t)(*p - start);
    char stackbuf[128];
    char *tok = (len < sizeof stackbuf) ? stackbuf : (char *)malloc(len + 1);
    if (!tok) { *p = start; return 0; }
    memcpy(tok, start, len);
    tok[len] = '\0';
    int rc = neverc_strconv_parse_float(tok, out);
    if (tok != stackbuf) free(tok);
    if (rc != NEVERC_STRCONV_OK && rc != NEVERC_STRCONV_ERR_RANGE) { *p = start; return 0; }
    return 1;
}

static int scan_string(const char **p, char *buf, size_t cap) {
    skip_ws(p);
    if (**p == '\0') return 0;
    size_t i = 0;
    while (**p && **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r') {
        if (i + 1 < cap) buf[i++] = **p;
        (*p)++;
    }
    if (i < cap) buf[i] = '\0';
    return i > 0 ? 1 : 0;
}

static int scan_hex(const char **p, uint64_t *out) {
    skip_ws(p);
    if (**p == '0' && ((*p)[1] == 'x' || (*p)[1] == 'X')) (*p) += 2;
    uint64_t val = 0;
    int found = 0;
    while (1) {
        char c = **p;
        if (c >= '0' && c <= '9') { val = val * 16 + (c - '0'); found = 1; }
        else if (c >= 'a' && c <= 'f') { val = val * 16 + (c - 'a' + 10); found = 1; }
        else if (c >= 'A' && c <= 'F') { val = val * 16 + (c - 'A' + 10); found = 1; }
        else break;
        (*p)++;
    }
    *out = val;
    return found;
}

int neverc_fmt_sscanf(const char *str, const char *format, ...) {
    if (!str || !format) return 0;
    va_list args;
    va_start(args, format);

    const char *sp = str;
    const char *fp = format;
    int matched = 0;

    while (*fp) {
        if (*fp == '%') {
            fp++;
            if (*fp == '%') { fp++; if (*sp == '%') sp++; continue; }

            int is_long = 0;
            if (*fp == 'l') { is_long = 1; fp++; if (*fp == 'l') fp++; }

            switch (*fp) {
            case 'd': case 'i': {
                int64_t val;
                if (!scan_int(&sp, &val)) goto done;
                if (is_long) *va_arg(args, long long *) = (long long)val;
                else *va_arg(args, int *) = (int)val;
                matched++;
                break;
            }
            case 'u': {
                uint64_t val;
                if (!scan_uint(&sp, &val)) goto done;
                if (is_long) *va_arg(args, unsigned long long *) = (unsigned long long)val;
                else *va_arg(args, unsigned int *) = (unsigned int)val;
                matched++;
                break;
            }
            case 'x': case 'X': {
                uint64_t val;
                if (!scan_hex(&sp, &val)) goto done;
                *va_arg(args, unsigned int *) = (unsigned int)val;
                matched++;
                break;
            }
            case 'f': case 'g': case 'e': {
                double val;
                if (!scan_float(&sp, &val)) goto done;
                *va_arg(args, double *) = val;
                matched++;
                break;
            }
            case 's': {
                char *buf = va_arg(args, char *);
                if (!scan_string(&sp, buf, 256)) goto done;
                matched++;
                break;
            }
            case 'c': {
                if (*sp == '\0') goto done;
                *va_arg(args, char *) = *sp++;
                matched++;
                break;
            }
            default: goto done;
            }
            fp++;
        } else if (*fp == ' ' || *fp == '\t' || *fp == '\n') {
            skip_ws(&sp);
            fp++;
        } else {
            if (*sp != *fp) goto done;
            sp++; fp++;
        }
    }
done:
    va_end(args);
    return matched;
}

int neverc_fmt_sscan(const char *str, ...) {
    if (!str) return 0;
    va_list args;
    va_start(args, str);
    const char *sp = str;
    int matched = 0;

    while (*sp) {
        skip_ws(&sp);
        if (*sp == '\0') break;
        int64_t ival;
        if (scan_int(&sp, &ival)) {
            int *p = va_arg(args, int *);
            if (!p) break;
            *p = (int)ival;
            matched++;
        } else {
            break;
        }
    }

    va_end(args);
    return matched;
}

int neverc_fmt_scanf(const char *format, ...) {
    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) return 0;
    va_list args;
    va_start(args, format);

    const char *sp = line;
    const char *fp = format;
    int matched = 0;

    while (*fp) {
        if (*fp == '%') {
            fp++;
            if (*fp == '%') { fp++; if (*sp == '%') sp++; continue; }
            int is_long = 0;
            if (*fp == 'l') { is_long = 1; fp++; if (*fp == 'l') fp++; }
            switch (*fp) {
            case 'd': case 'i': {
                int64_t val;
                if (!scan_int(&sp, &val)) goto sdone;
                if (is_long) *va_arg(args, long long *) = (long long)val;
                else *va_arg(args, int *) = (int)val;
                matched++;
                break;
            }
            case 'f': case 'g': case 'e': {
                double val;
                if (!scan_float(&sp, &val)) goto sdone;
                *va_arg(args, double *) = val;
                matched++;
                break;
            }
            case 's': {
                char *buf = va_arg(args, char *);
                if (!scan_string(&sp, buf, 256)) goto sdone;
                matched++;
                break;
            }
            default: goto sdone;
            }
            fp++;
        } else if (*fp == ' ' || *fp == '\t' || *fp == '\n') {
            skip_ws(&sp);
            fp++;
        } else {
            if (*sp != *fp) goto sdone;
            sp++; fp++;
        }
    }
sdone:
    va_end(args);
    return matched;
}

int neverc_fmt_scan(int *out_int) {
    char line[256];
    if (!fgets(line, sizeof(line), stdin)) return 0;
    const char *p = line;
    int64_t val;
    if (scan_int(&p, &val)) { *out_int = (int)val; return 1; }
    return 0;
}

int neverc_fmt_fscanf(FILE *f, const char *format, ...) {
    if (!f || !format) return 0;
    char line[4096];
    if (!fgets(line, sizeof(line), f)) return 0;

    va_list args;
    va_start(args, format);
    const char *sp = line;
    const char *fp = format;
    int matched = 0;

    while (*fp) {
        if (*fp == '%') {
            fp++;
            if (*fp == '%') { fp++; if (*sp == '%') sp++; continue; }
            int is_long = 0;
            if (*fp == 'l') { is_long = 1; fp++; if (*fp == 'l') fp++; }
            switch (*fp) {
            case 'd': case 'i': {
                int64_t val;
                if (!scan_int(&sp, &val)) goto fdone;
                if (is_long) *va_arg(args, long long *) = (long long)val;
                else *va_arg(args, int *) = (int)val;
                matched++;
                break;
            }
            case 'f': case 'g': case 'e': {
                double val;
                if (!scan_float(&sp, &val)) goto fdone;
                *va_arg(args, double *) = val;
                matched++;
                break;
            }
            case 's': {
                char *buf = va_arg(args, char *);
                if (!scan_string(&sp, buf, 256)) goto fdone;
                matched++;
                break;
            }
            default: goto fdone;
            }
            fp++;
        } else if (*fp == ' ' || *fp == '\t' || *fp == '\n') {
            skip_ws(&sp);
            fp++;
        } else {
            if (*sp != *fp) goto fdone;
            sp++; fp++;
        }
    }
fdone:
    va_end(args);
    return matched;
}

int neverc_fmt_appendf(char *buf, size_t cap, const char *format, ...) {
    size_t existing;
    if (!format || !append_position(buf, cap, &existing)) return 0;

    va_list args;
    va_start(args, format);
    char *s = neverc_fmt_vsprintf(format, args);
    va_end(args);
    if (!s) return 0;
    size_t slen = my_strlen(s);
    size_t space = cap - existing - 1;
    size_t copy = slen < space ? slen : space;
    for (size_t i = 0; i < copy; i++) buf[existing + i] = s[i];
    buf[existing + copy] = '\0';
    free(s);
    return (int)copy;
}

int neverc_fmt_append(char *buf, size_t cap, const char *s) {
    size_t existing;
    if (!s || !append_position(buf, cap, &existing)) return 0;
    size_t slen = my_strlen(s);
    size_t space = cap - existing - 1;
    size_t copy = slen < space ? slen : space;
    for (size_t i = 0; i < copy; i++) buf[existing + i] = s[i];
    buf[existing + copy] = '\0';
    return (int)copy;
}

int neverc_fmt_appendln(char *buf, size_t cap, const char *s) {
    size_t existing;
    if (!s || !append_position(buf, cap, &existing)) return 0;
    size_t slen = my_strlen(s);
    size_t space = cap - existing - 1;
    size_t need = slen + 1;
    size_t copy = need < space ? need : space;
    size_t scopy = copy > 0 ? (copy > slen ? slen : copy) : 0;
    for (size_t i = 0; i < scopy; i++) buf[existing + i] = s[i];
    if (scopy < copy) buf[existing + scopy] = '\n';
    buf[existing + copy] = '\0';
    return (int)copy;
}

char *neverc_fmt_sprintfln(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char *s = neverc_fmt_vsprintf(format, args);
    va_end(args);
    if (!s) return NULL;
    size_t len = my_strlen(s);
    char *out = (char *)realloc(s, len + 2);
    if (!out) { free(s); return NULL; }
    out[len] = '\n';
    out[len + 1] = '\0';
    return out;
}

int neverc_fmt_fscan(FILE *f, int *out_int) {
    if (!f || !out_int) return 0;
    char line[256];
    if (!fgets(line, sizeof(line), f)) return 0;
    const char *p = line;
    int64_t val;
    if (scan_int(&p, &val)) { *out_int = (int)val; return 1; }
    return 0;
}

int neverc_fmt_scanln(const char *format, ...) {
    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) return 0;
    size_t len = my_strlen(line);
    if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

    va_list args;
    va_start(args, format);
    const char *sp = line;
    const char *fp = format;
    int matched = 0;

    while (*fp && *sp) {
        if (*fp == '%') {
            fp++;
            int is_long = 0;
            if (*fp == 'l') { is_long = 1; fp++; if (*fp == 'l') fp++; }
            switch (*fp) {
            case 'd': case 'i': {
                int64_t val;
                if (!scan_int(&sp, &val)) goto sldone;
                if (is_long) *va_arg(args, long long *) = (long long)val;
                else *va_arg(args, int *) = (int)val;
                matched++;
                break;
            }
            case 'f': {
                double val;
                if (!scan_float(&sp, &val)) goto sldone;
                *va_arg(args, double *) = val;
                matched++;
                break;
            }
            case 's': {
                char *buf = va_arg(args, char *);
                if (!scan_string(&sp, buf, 256)) goto sldone;
                matched++;
                break;
            }
            default: goto sldone;
            }
            fp++;
        } else {
            if (*sp != *fp) goto sldone;
            sp++; fp++;
        }
    }
sldone:
    va_end(args);
    return matched;
}

int neverc_fmt_sscanln(const char *str, const char *format, ...) {
    if (!str || !format) return 0;
    char line[4096];
    size_t slen = my_strlen(str);
    size_t copy = slen < sizeof(line) - 1 ? slen : sizeof(line) - 1;
    for (size_t i = 0; i < copy; i++) line[i] = str[i];
    line[copy] = '\0';
    for (size_t i = 0; i < copy; i++) {
        if (line[i] == '\n') { line[i] = '\0'; break; }
    }

    va_list args;
    va_start(args, format);
    const char *sp = line;
    const char *fp = format;
    int matched = 0;

    while (*fp && *sp) {
        if (*fp == '%') {
            fp++;
            int is_long = 0;
            if (*fp == 'l') { is_long = 1; fp++; if (*fp == 'l') fp++; }
            switch (*fp) {
            case 'd': case 'i': {
                int64_t val;
                if (!scan_int(&sp, &val)) goto ssldone;
                if (is_long) *va_arg(args, long long *) = (long long)val;
                else *va_arg(args, int *) = (int)val;
                matched++;
                break;
            }
            case 'f': {
                double val;
                if (!scan_float(&sp, &val)) goto ssldone;
                *va_arg(args, double *) = val;
                matched++;
                break;
            }
            case 's': {
                char *buf = va_arg(args, char *);
                if (!scan_string(&sp, buf, 256)) goto ssldone;
                matched++;
                break;
            }
            default: goto ssldone;
            }
            fp++;
        } else {
            if (*sp != *fp) goto ssldone;
            sp++; fp++;
        }
    }
ssldone:
    va_end(args);
    return matched;
}

int neverc_fmt_fscanln(FILE *f, const char *format, ...) {
    if (!f || !format) return 0;
    char line[4096];
    if (!fgets(line, sizeof(line), f)) return 0;
    size_t len = my_strlen(line);
    if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

    va_list args;
    va_start(args, format);
    const char *sp = line;
    const char *fp = format;
    int matched = 0;

    while (*fp && *sp) {
        if (*fp == '%') {
            fp++;
            int is_long = 0;
            if (*fp == 'l') { is_long = 1; fp++; if (*fp == 'l') fp++; }
            switch (*fp) {
            case 'd': case 'i': {
                int64_t val;
                if (!scan_int(&sp, &val)) goto fldone;
                if (is_long) *va_arg(args, long long *) = (long long)val;
                else *va_arg(args, int *) = (int)val;
                matched++;
                break;
            }
            case 'f': {
                double val;
                if (!scan_float(&sp, &val)) goto fldone;
                *va_arg(args, double *) = val;
                matched++;
                break;
            }
            case 's': {
                char *buf = va_arg(args, char *);
                if (!scan_string(&sp, buf, 256)) goto fldone;
                matched++;
                break;
            }
            default: goto fldone;
            }
            fp++;
        } else {
            if (*sp != *fp) goto fldone;
            sp++; fp++;
        }
    }
fldone:
    va_end(args);
    return matched;
}
