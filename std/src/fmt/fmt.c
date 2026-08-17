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

/* Go measures %s width/precision in runes. Invalid bytes count as one rune;
 * a leading byte that is not a well-formed sequence must not swallow the
 * following byte (e.g. 0xC0 'A' is two runes, not one). */
static size_t utf8_rune_len(const char *s, size_t remaining) {
    if (remaining == 0) return 0;
    unsigned char b = (unsigned char)s[0];
    if (b < 0x80) return 1;
    if ((b & 0xE0) == 0xC0 && remaining >= 2 &&
        ((unsigned char)s[1] & 0xC0) == 0x80) {
        unsigned r = ((unsigned)(b & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
        return r >= 0x80 ? 2 : 1;
    }
    if ((b & 0xF0) == 0xE0 && remaining >= 3 &&
        ((unsigned char)s[1] & 0xC0) == 0x80 &&
        ((unsigned char)s[2] & 0xC0) == 0x80) {
        unsigned r = ((unsigned)(b & 0x0F) << 12) |
                     ((unsigned)((unsigned char)s[1] & 0x3F) << 6) |
                     ((unsigned char)s[2] & 0x3F);
        if (r >= 0x800 && (r < 0xD800 || r > 0xDFFF)) return 3;
        return 1;
    }
    if ((b & 0xF8) == 0xF0 && remaining >= 4 &&
        ((unsigned char)s[1] & 0xC0) == 0x80 &&
        ((unsigned char)s[2] & 0xC0) == 0x80 &&
        ((unsigned char)s[3] & 0xC0) == 0x80) {
        unsigned r = ((unsigned)(b & 0x07) << 18) |
                     ((unsigned)((unsigned char)s[1] & 0x3F) << 12) |
                     ((unsigned)((unsigned char)s[2] & 0x3F) << 6) |
                     ((unsigned char)s[3] & 0x3F);
        if (r >= 0x10000 && r <= 0x10FFFF) return 4;
        return 1;
    }
    return 1;
}

static size_t utf8_span_runes(const char *s, size_t slen, int max_runes,
                              int *nrunes) {
    size_t i = 0;
    int nr = 0;
    while (i < slen && (max_runes < 0 || nr < max_runes)) {
        size_t w = utf8_rune_len(s + i, slen - i);
        if (w == 0) w = 1;
        i += w;
        nr++;
    }
    if (nrunes) *nrunes = nr;
    return i;
}

static int encode_rune(char *dst, uint32_t r) {
    if (r <= 0x7F) {
        dst[0] = (char)r;
        return 1;
    }
    if (r <= 0x7FF) {
        dst[0] = (char)(0xC0 | (r >> 6));
        dst[1] = (char)(0x80 | (r & 0x3F));
        return 2;
    }
    if ((r >= 0xD800 && r <= 0xDFFF) || r > 0x10FFFF)
        r = 0xFFFD;
    if (r <= 0xFFFF) {
        dst[0] = (char)(0xE0 | (r >> 12));
        dst[1] = (char)(0x80 | ((r >> 6) & 0x3F));
        dst[2] = (char)(0x80 | (r & 0x3F));
        return 3;
    }
    dst[0] = (char)(0xF0 | (r >> 18));
    dst[1] = (char)(0x80 | ((r >> 12) & 0x3F));
    dst[2] = (char)(0x80 | ((r >> 6) & 0x3F));
    dst[3] = (char)(0x80 | (r & 0x3F));
    return 4;
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

/* Float formatting delegates to strconv's correctly-rounded engine
 * (Ryu shortest + exact decimal), shared so fmt and strconv stay consistent. */
static int fmt_float_f(char *buf, size_t cap, double val, int prec) {
    if (prec < 0) prec = 6;
    return neverc_strconv_format_float(val, 'f', prec, buf, cap);
}
static int fmt_float_e(char *buf, size_t cap, double val, int prec, int uppercase) {
    if (prec < 0) prec = 6;
    return neverc_strconv_format_float(
        val, uppercase ? 'E' : 'e', prec, buf, cap);
}
/* Go %g/%G default precision is 6 significant digits (not shortest). */
static int fmt_float_g(char *buf, size_t cap, double val, int prec, int uppercase) {
    if (prec < 0) prec = 6;
    return neverc_strconv_format_float(
        val, uppercase ? 'G' : 'g', prec, buf, cap);
}

/* Go fmt.fmtFloat sharp: force a decimal point, and for g/G keep trailing zeros
 * up to the requested significant-digit count. */
static int apply_float_sharp(char *num, int tlen, size_t cap, char verb, int prec) {
    if (!num || tlen <= 0 || (size_t)tlen >= cap) return -1;
    int start = (num[0] == '-' || num[0] == '+') ? 1 : 0;
    if (start >= tlen) return tlen;
    if (num[start] == 'I' || num[start] == 'N') return tlen;

    int digits = 0;
    if (verb == 'g' || verb == 'G')
        digits = prec < 0 ? 6 : prec;

    int exp_at = -1;
    for (int i = start; i < tlen; i++) {
        if (num[i] == 'e' || num[i] == 'E') {
            exp_at = i;
            break;
        }
    }
    int body_end = exp_at >= 0 ? exp_at : tlen;
    int tail_len = tlen - body_end;

    int has_dot = 0, saw_nz = 0;
    for (int i = start; i < body_end; i++) {
        if (num[i] == '.') {
            has_dot = 1;
            continue;
        }
        if (num[i] != '0') saw_nz = 1;
        if (saw_nz) digits--;
    }

    int extra_dot = !has_dot;
    if (!has_dot && body_end - start == 1 && num[start] == '0')
        digits--;
    int zeros = digits > 0 ? digits : 0;
    size_t extra = (size_t)extra_dot + (size_t)zeros;
    if ((size_t)tlen + extra >= cap) return -1;

    if (tail_len > 0 && extra > 0)
        memmove(num + body_end + extra, num + body_end, (size_t)tail_len);
    int w = body_end;
    if (extra_dot) num[w++] = '.';
    for (int z = 0; z < zeros; z++) num[w++] = '0';
    int nlen = tlen + (int)extra;
    num[nlen] = '\0';
    return nlen;
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
        if (i >= flen) {
            buf_putc(&buf, '%');
            break;
        }

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
        if (has_width && width < 0) {
            flag_minus = 1;
            if (width == INT_MIN) width = INT_MAX;
            else width = -width;
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

        if (i >= flen) {
            buf_putc(&buf, '%');
            break;
        }
        char verb = format[i];
        char tmp[512];   /* large enough for %f of values up to ~1e308 */
        int tlen = 0;

        switch (verb) {
        case '%':
            buf_putc(&buf, '%');
            continue;
        case 'd': case 'i': {
            int64_t val = is_longlong ? va_arg(args, long long) :
                          is_long ? (int64_t)va_arg(args, long) :
                          (int64_t)va_arg(args, int);
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
            tlen = encode_rune(tmp, (uint32_t)va_arg(args, int));
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            if (!s) s = "(null)";
            size_t slen = my_strlen(s);
            int nrunes = 0;
            slen = utf8_span_runes(s, slen, prec, &nrunes);
            int pad = (has_width && width > nrunes) ? width - nrunes : 0;
            if (!flag_minus) buf_pad(&buf, ' ', pad);
            buf_puts(&buf, s, slen);
            if (flag_minus) buf_pad(&buf, ' ', pad);
            continue;
        }
        case 'f': {
            double val = va_arg(args, double);
            tlen = fmt_float_f(tmp, sizeof tmp, val, prec);
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
            /* Hex digits only; "0x" is an alt prefix so zero-padding and %#p
             * (Go: suppress 0x) apply around it instead of splitting "0x". */
            void *ptr = va_arg(args, void *);
            tlen = fmt_uint_base(tmp, (uint64_t)(uintptr_t)ptr, 16, 0);
            break;
        }
        default:
            goto format_fail;
        }
        if (tlen < 0) goto format_fail;
        if (flag_hash &&
            (verb == 'f' || verb == 'e' || verb == 'E' ||
             verb == 'g' || verb == 'G')) {
            tlen = apply_float_sharp(tmp, tlen, sizeof tmp, verb, prec);
            if (tlen < 0) goto format_fail;
        }

        /* Apply width/padding */
        int is_float_verb =
            verb == 'f' || verb == 'e' || verb == 'E' ||
            verb == 'g' || verb == 'G';
        int is_int_verb =
            verb == 'd' || verb == 'i' || verb == 'u' ||
            verb == 'x' || verb == 'X' || verb == 'o' || verb == 'b' ||
            verb == 'p';
        int is_signed_verb =
            verb == 'd' || verb == 'i' || is_float_verb;
        int formatted_has_sign =
            tlen > 0 && (tmp[0] == '+' || tmp[0] == '-');
        int body_offset =
            is_signed_verb && formatted_has_sign ? 1 : 0;
        char sign_prefix = body_offset ? tmp[0] : '\0';
        if (sign_prefix == '+' && flag_space && !flag_plus)
            sign_prefix = ' ';
        if (!sign_prefix && is_signed_verb) {
            if (flag_plus)
                sign_prefix = '+';
            else if (flag_space)
                sign_prefix = ' ';
        }
        int body_len = tlen - body_offset;
        if (is_int_verb && prec >= 0) {
            int is_zero_body =
                body_len == 1 && tmp[body_offset] == '0';
            if (prec == 0 && is_zero_body) {
                body_len = 0;
            } else if (prec > body_len) {
                size_t extra = (size_t)prec - (size_t)body_len;
                if (tlen < 0 || extra >= sizeof(tmp) ||
                    (size_t)tlen > sizeof(tmp) - extra)
                    goto format_fail;
                memmove(tmp + body_offset + (int)extra,
                        tmp + body_offset, (size_t)body_len);
                memset(tmp + body_offset, '0', extra);
                tlen += (int)extra;
                body_len += (int)extra;
            }
        }
        int formatted_is_special =
            is_float_verb &&
            ((body_len == 3 &&
              memcmp(tmp + body_offset, "Inf", 3) == 0) ||
             (body_len == 3 &&
              memcmp(tmp + body_offset, "NaN", 3) == 0));
        const char *alt_prefix = NULL;
        int alt_len = 0;
        if (flag_hash && !is_float_verb) {
            if (verb == 'x') { alt_prefix = "0x"; alt_len = 2; }
            else if (verb == 'X') { alt_prefix = "0X"; alt_len = 2; }
            else if (verb == 'b') { alt_prefix = "0b"; alt_len = 2; }
            else if (verb == 'o' &&
                     (body_len == 0 || tmp[body_offset] != '0')) {
                alt_prefix = "0";
                alt_len = 1;
            }
        } else if (verb == 'p') {
            alt_prefix = "0x";
            alt_len = 2;
        }
        /* Zero-pad only numeric verbs. Precision on integers suppresses 0
         * (Go/C99). %c is a rune: width is spaces, never '0'. */
        int use_zero_padding =
            flag_zero && !formatted_is_special &&
            (is_int_verb || is_float_verb) &&
            !(is_int_verb && prec >= 0);
        /* %c is one rune; width is measured in runes, not UTF-8 bytes. */
        int content_width = (verb == 'c') ? (tlen > 0 ? 1 : 0) : body_len;
        int total = content_width + (sign_prefix ? 1 : 0) + alt_len;
        int pad = (has_width && width > total) ? width - total : 0;

        if (!flag_minus && !use_zero_padding) buf_pad(&buf, ' ', pad);
        if (sign_prefix) buf_putc(&buf, sign_prefix);
        if (alt_prefix) buf_puts(&buf, alt_prefix, (size_t)alt_len);
        if (!flag_minus && use_zero_padding) buf_pad(&buf, '0', pad);
        buf_puts(&buf, tmp + body_offset, (size_t)body_len);
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
    if (!f) {
        free(s);
        return -1;
    }
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
    const char *start = *p;
    if (**p == '\0') return 0;
    int neg = 0;
    if (**p == '-') { neg = 1; (*p)++; }
    else if (**p == '+') { (*p)++; }
    if (**p < '0' || **p > '9') { *p = start; return 0; }

    uint64_t val = 0;
    const uint64_t limit = neg
        ? (uint64_t)INT64_MAX + 1U
        : (uint64_t)INT64_MAX;
    while (**p >= '0' && **p <= '9') {
        unsigned digit = (unsigned)(**p - '0');
        if (val > (limit - digit) / 10U) {
            *p = start;
            return 0;
        }
        val = val * 10U + digit;
        (*p)++;
    }
    if (neg)
        *out = val == (uint64_t)INT64_MAX + 1U
            ? INT64_MIN
            : -(int64_t)val;
    else
        *out = (int64_t)val;
    return 1;
}

static int scan_uint(const char **p, uint64_t *out) {
    skip_ws(p);
    const char *start = *p;
    if (**p < '0' || **p > '9') return 0;
    uint64_t val = 0;
    while (**p >= '0' && **p <= '9') {
        unsigned digit = (unsigned)(**p - '0');
        if (val > (UINT64_MAX - digit) / 10U) {
            *p = start;
            return 0;
        }
        val = val * 10U + digit;
        (*p)++;
    }
    *out = val;
    return 1;
}

static int scan_float(const char **p, double *out) {
    skip_ws(p);
    const char *start = *p;
    if (**p == '-' || **p == '+') (*p)++;

    if (**p == 'i' || **p == 'I' || **p == 'n' || **p == 'N') {
        /* Keep the whole alphabetic token together so malformed near-matches
         * such as "infix" are rejected by strconv rather than accepted as Inf. */
        while ((**p >= 'a' && **p <= 'z') ||
               (**p >= 'A' && **p <= 'Z'))
            (*p)++;
    } else {
        if ((**p < '0' || **p > '9') && **p != '.') {
            *p = start;
            return 0;
        }

        /* Delimit the numeric token, then hand it to strconv's correctly-rounded
         * parser instead of accumulating in floating point. A complete hex
         * float (0x…p…) is one token; a bare 0x prefix is not, so "0x1p0"
         * must not be scanned as 0. */
        int took_hex = 0;
        if (**p == '0' && ((*p)[1] == 'x' || (*p)[1] == 'X')) {
            const char *q = *p + 2;
            int saw_hex = 0;
            while ((*q >= '0' && *q <= '9') ||
                   (*q >= 'a' && *q <= 'f') ||
                   (*q >= 'A' && *q <= 'F')) {
                q++;
                saw_hex = 1;
            }
            if (*q == '.') {
                q++;
                while ((*q >= '0' && *q <= '9') ||
                       (*q >= 'a' && *q <= 'f') ||
                       (*q >= 'A' && *q <= 'F')) {
                    q++;
                    saw_hex = 1;
                }
            }
            if (saw_hex && (*q == 'p' || *q == 'P')) {
                const char *r = q + 1;
                if (*r == '+' || *r == '-') r++;
                if (*r >= '0' && *r <= '9') {
                    while (*r >= '0' && *r <= '9') r++;
                    *p = r;
                    took_hex = 1;
                }
            }
            if (!took_hex) {
                /* 0x/0X starts a hex-float token. A bare prefix must not
                 * fall through to decimal and silently parse as 0. */
                *p = start;
                return 0;
            }
        }
        if (!took_hex) {
            while (**p >= '0' && **p <= '9') (*p)++;
            if (**p == '.') { (*p)++; while (**p >= '0' && **p <= '9') (*p)++; }
            if (**p == 'e' || **p == 'E') {
                const char *esave = *p;
                (*p)++;
                if (**p == '-' || **p == '+') (*p)++;
                if (**p >= '0' && **p <= '9') {
                    while (**p >= '0' && **p <= '9') (*p)++;
                } else {
                    *p = esave;        /* lone 'e' is not part of the number */
                }
            }
        }
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

static int scan_string(const char **p, char *buf, size_t max_chars) {
    skip_ws(p);
    if (!buf || max_chars == 0 || **p == '\0') return 0;
    size_t i = 0;
    while (i < max_chars && **p && **p != ' ' && **p != '\t' &&
           **p != '\n' && **p != '\r') {
        buf[i++] = **p;
        (*p)++;
    }
    buf[i] = '\0';
    return i > 0 ? 1 : 0;
}

static int scan_hex(const char **p, uint64_t *out) {
    skip_ws(p);
    const char *start = *p;
    if (**p == '0' && ((*p)[1] == 'x' || (*p)[1] == 'X')) (*p) += 2;
    uint64_t val = 0;
    int found = 0;
    while (1) {
        char c = **p;
        unsigned digit;
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A' + 10);
        else break;
        if (val > (UINT64_MAX - digit) / 16U) {
            *p = start;
            return 0;
        }
        val = val * 16U + digit;
        found = 1;
        (*p)++;
    }
    if (!found) {
        *p = start;
        return 0;
    }
    *out = val;
    return 1;
}

static int scan_value_fits_int(int64_t value) {
    return value >= INT_MIN && value <= INT_MAX;
}

static int scan_value_fits_uint(uint64_t value) {
    return value <= UINT_MAX;
}

typedef enum {
    SCAN_LENGTH_NONE = 0,
    SCAN_LENGTH_LONG,
    SCAN_LENGTH_LONG_LONG
} scan_length_t;

static int scan_parse_size(const char **format, size_t *width,
                           int *has_width) {
    *width = 0;
    *has_width = 0;
    while (**format >= '0' && **format <= '9') {
        unsigned digit = (unsigned)(**format - '0');
        if (*width > (SIZE_MAX - digit) / 10U)
            return 0;
        *width = *width * 10U + digit;
        *has_width = 1;
        (*format)++;
    }
    return !*has_width || *width != 0;
}

static int scan_formatted(const char *str, const char *format, va_list args) {
    if (!str || !format) return 0;

    va_list ap;
    va_copy(ap, args);
    const char *sp = str;
    const char *fp = format;
    int matched = 0;

    while (*fp) {
        if (*fp != '%') {
            if (*fp == ' ' || *fp == '\t' || *fp == '\n' || *fp == '\r') {
                skip_ws(&sp);
                fp++;
                continue;
            }
            if (*sp != *fp)
                break;
            sp++;
            fp++;
            continue;
        }

        fp++;
        if (*fp == '%') {
            if (*sp != '%')
                break;
            sp++;
            fp++;
            continue;
        }

        size_t width;
        int has_width;
        if (!scan_parse_size(&fp, &width, &has_width))
            break;

        scan_length_t length = SCAN_LENGTH_NONE;
        if (*fp == 'l') {
            length = SCAN_LENGTH_LONG;
            fp++;
            if (*fp == 'l') {
                length = SCAN_LENGTH_LONG_LONG;
                fp++;
            }
        }

        switch (*fp) {
        case 'd':
        case 'i': {
            int64_t value;
            if (has_width || !scan_int(&sp, &value))
                goto done;
            if (length == SCAN_LENGTH_LONG_LONG) {
                long long *out = va_arg(ap, long long *);
                if (!out) goto done;
                *out = (long long)value;
            } else if (length == SCAN_LENGTH_LONG) {
                if (value < (int64_t)LONG_MIN || value > (int64_t)LONG_MAX)
                    goto done;
                long *out = va_arg(ap, long *);
                if (!out) goto done;
                *out = (long)value;
            } else {
                if (!scan_value_fits_int(value))
                    goto done;
                int *out = va_arg(ap, int *);
                if (!out) goto done;
                *out = (int)value;
            }
            matched++;
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t value;
            int ok = *fp == 'u' ? scan_uint(&sp, &value)
                                : scan_hex(&sp, &value);
            if (has_width || !ok)
                goto done;
            if (length == SCAN_LENGTH_LONG_LONG) {
                unsigned long long *out =
                    va_arg(ap, unsigned long long *);
                if (!out) goto done;
                *out = (unsigned long long)value;
            } else if (length == SCAN_LENGTH_LONG) {
                if (value > (uint64_t)ULONG_MAX)
                    goto done;
                unsigned long *out = va_arg(ap, unsigned long *);
                if (!out) goto done;
                *out = (unsigned long)value;
            } else {
                if (!scan_value_fits_uint(value))
                    goto done;
                unsigned int *out = va_arg(ap, unsigned int *);
                if (!out) goto done;
                *out = (unsigned int)value;
            }
            matched++;
            break;
        }
        case 'f':
        case 'g':
        case 'e': {
            double value;
            if (has_width || length == SCAN_LENGTH_LONG_LONG ||
                !scan_float(&sp, &value))
                goto done;
            double *out = va_arg(ap, double *);
            if (!out) goto done;
            *out = value;
            matched++;
            break;
        }
        case 's': {
            if (!has_width || length != SCAN_LENGTH_NONE)
                goto done;
            char *out = va_arg(ap, char *);
            if (!scan_string(&sp, out, width))
                goto done;
            matched++;
            break;
        }
        case 'c': {
            if (has_width || length != SCAN_LENGTH_NONE || *sp == '\0')
                goto done;
            char *out = va_arg(ap, char *);
            if (!out) goto done;
            *out = *sp++;
            matched++;
            break;
        }
        default:
            goto done;
        }
        fp++;
    }

done:
    va_end(ap);
    return matched;
}

int neverc_fmt_sscanf(const char *str, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int matched = scan_formatted(str, format, args);
    va_end(args);
    return matched;
}

int neverc_fmt_sscan_ints(const char *str, int *outputs,
                          size_t output_count) {
    if (!str || (!outputs && output_count != 0)) return 0;
    const char *sp = str;
    size_t matched = 0;
    while (matched < output_count) {
        int64_t value;
        if (!scan_int(&sp, &value) || !scan_value_fits_int(value))
            break;
        outputs[matched++] = (int)value;
    }
    return matched > (size_t)INT_MAX ? INT_MAX : (int)matched;
}

int neverc_fmt_sscan(const char *str, ...) {
    if (!str) return 0;
    va_list args;
    va_start(args, str);
    int *out_int = va_arg(args, int *);
    va_end(args);
    return neverc_fmt_sscan_ints(str, out_int, out_int ? 1U : 0U);
}

int neverc_fmt_scanf(const char *format, ...) {
    if (!format) return 0;
    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) return 0;
    va_list args;
    va_start(args, format);
    int matched = scan_formatted(line, format, args);
    va_end(args);
    return matched;
}

int neverc_fmt_scan(int *out_int) {
    if (!out_int) return 0;
    char line[256];
    if (!fgets(line, sizeof(line), stdin)) return 0;
    const char *p = line;
    int64_t val;
    if (scan_int(&p, &val) && scan_value_fits_int(val)) {
        *out_int = (int)val;
        return 1;
    }
    return 0;
}

int neverc_fmt_fscanf(FILE *f, const char *format, ...) {
    if (!f || !format) return 0;
    char line[4096];
    if (!fgets(line, sizeof(line), f)) return 0;

    va_list args;
    va_start(args, format);
    int matched = scan_formatted(line, format, args);
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
    if (scan_int(&p, &val) && scan_value_fits_int(val)) {
        *out_int = (int)val;
        return 1;
    }
    return 0;
}

int neverc_fmt_scanln(const char *format, ...) {
    if (!format) return 0;
    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) return 0;
    size_t len = my_strlen(line);
    if (len > 0 && line[len-1] == '\n') line[--len] = '\0';

    va_list args;
    va_start(args, format);
    int matched = scan_formatted(line, format, args);
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
    int matched = scan_formatted(line, format, args);
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
    int matched = scan_formatted(line, format, args);
    va_end(args);
    return matched;
}
