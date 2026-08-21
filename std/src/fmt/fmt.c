#include "neverc/std/fmt.h"
#include "neverc/std/strconv.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include <math.h>

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
                              size_t *nrunes) {
    size_t i = 0;
    size_t nr = 0;
    while (i < slen && (max_runes < 0 || nr < (size_t)max_runes)) {
        size_t w = utf8_rune_len(s + i, slen - i);
        if (w == 0) w = 1;
        i += w;
        if (nr == SIZE_MAX) break;
        nr++;
    }
    if (nrunes) *nrunes = nr;
    return i;
}

static int fmt_return_len(size_t len) {
    return len > (size_t)INT_MAX ? -1 : (int)len;
}

static int fmt_fwrite(FILE *f, const char *s, size_t len) {
    if (!f)
        return -1;
    if (len == 0)
        return 0;
    size_t wrote = fwrite(s, 1, len, f);
    if (wrote != len || ferror(f))
        return -1;
    return fmt_return_len(wrote);
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
/* Go %g/%G default is shortest (prec=-1); %#g still pads to 6 in apply_float_sharp. */
static int fmt_float_g(char *buf, size_t cap, double val, int prec, int uppercase) {
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

/* Core formatting engine. out_len, when set, is the formatted byte count
 * including interior NULs from %c of 0 — strlen() cannot recover that. */
static char *fmt_vsprintf_n(const char *format, va_list args, size_t *out_len) {
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

        /* Go fmt.tooLarge: width/prec above 1e6 is a format error, not a
         * multi-gigabyte pad allocation. */
        enum { FMT_MAX_WIDPREC = 1000000 };
        if (has_width && width > FMT_MAX_WIDPREC) goto format_fail;
        if (prec > FMT_MAX_WIDPREC) goto format_fail;

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
            size_t nrunes = 0;
            slen = utf8_span_runes(s, slen, prec, &nrunes);
            if (nrunes > (size_t)INT_MAX) goto format_fail;
            int pad = (has_width && width > (int)nrunes)
                          ? width - (int)nrunes : 0;
            /* Go fmt: the '0' flag zero-pads strings unless '-' is set
             * (fmt_test.go `%05s` → `00abc`; issue 56486 documented this). */
            char padc = (flag_zero && !flag_minus) ? '0' : ' ';
            if (!flag_minus) buf_pad(&buf, padc, pad);
            buf_puts(&buf, s, slen);
            if (flag_minus) buf_pad(&buf, ' ', pad);
            continue;
        }
        case 'f': case 'F': {
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
            (verb == 'f' || verb == 'F' || verb == 'e' || verb == 'E' ||
             verb == 'g' || verb == 'G')) {
            tlen = apply_float_sharp(tmp, tlen, sizeof tmp, verb, prec);
            if (tlen < 0) goto format_fail;
        }

        /* Apply width/padding */
        int is_float_verb =
            verb == 'f' || verb == 'F' || verb == 'e' || verb == 'E' ||
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
        /* Go writePadding: '0' pads numbers and %c runes. Integer precision
         * suppresses 0. Inf/NaN stay space-padded. */
        int use_zero_padding =
            flag_zero && !formatted_is_special &&
            (is_int_verb || is_float_verb || verb == 'c') &&
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
    if (out_len) *out_len = buf.len > 0 ? buf.len - 1 : 0;
    return buf.data;

format_fail:
    free(buf.data);
    return NULL;
}

char *neverc_fmt_vsprintf(const char *format, va_list args) {
    return fmt_vsprintf_n(format, args, NULL);
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
    size_t len = 0;
    char *s = fmt_vsprintf_n(format, args, &len);
    va_end(args);
    if (!s) return -1;
    if (!f) {
        free(s);
        return -1;
    }
    int n = fmt_fwrite(f, s, len);
    free(s);
    return n;
}

int neverc_fmt_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    size_t len = 0;
    char *s = fmt_vsprintf_n(format, args, &len);
    va_end(args);
    if (!s) return -1;
    int n = fmt_fwrite(stdout, s, len);
    free(s);
    return n;
}

int neverc_fmt_println(const char *format, ...) {
    va_list args;
    va_start(args, format);
    size_t len = 0;
    char *s = fmt_vsprintf_n(format, args, &len);
    va_end(args);
    if (!s) return -1;
    if (fmt_fwrite(stdout, s, len) < 0 ||
        fmt_fwrite(stdout, "\n", 1) < 0) {
        free(s);
        return -1;
    }
    free(s);
    if (len >= (size_t)INT_MAX) return -1;
    return (int)len + 1;
}

char *neverc_fmt_sprint(const char *s) {
    if (!s) s = "";
    size_t len = my_strlen(s);
    if (len == SIZE_MAX) return NULL;
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i++) out[i] = s[i];
    out[len] = '\0';
    return out;
}

char *neverc_fmt_sprintln(const char *s) {
    if (!s) s = "";
    size_t len = my_strlen(s);
    if (len > SIZE_MAX - 2) return NULL;
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
    return fmt_fwrite(f, s, len);
}

int neverc_fmt_fprintln(FILE *f, const char *s) {
    if (!s || !f) return -1;
    size_t len = my_strlen(s);
    if (fmt_fwrite(f, s, len) < 0 || fmt_fwrite(f, "\n", 1) < 0)
        return -1;
    if (len >= (size_t)INT_MAX) return -1;
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

/* Go fmt/scan.go isSpace + unicode.IsSpace. Formatted Scanf treats only
 * U+000A as a newline; other Unicode spaces still skip. */
static int scan_decode_rune(const char *s, uint32_t *r, int *n) {
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) {
        *r = c;
        *n = 1;
        return 1;
    }
    int need = 0;
    uint32_t minv = 0;
    if ((c & 0xE0) == 0xC0) {
        need = 2;
        *r = c & 0x1F;
        minv = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        need = 3;
        *r = c & 0x0F;
        minv = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        need = 4;
        *r = c & 0x07;
        minv = 0x10000;
    } else {
        *r = c;
        *n = 1;
        return 0;
    }
    int i;
    for (i = 1; i < need; i++) {
        unsigned char b = (unsigned char)s[i];
        if ((b & 0xC0) != 0x80) {
            *r = c;
            *n = 1;
            return 0;
        }
        *r = (*r << 6) | (uint32_t)(b & 0x3F);
    }
    if (*r < minv || *r > 0x10FFFF || (*r >= 0xD800 && *r <= 0xDFFF)) {
        *r = c;
        *n = 1;
        return 0;
    }
    *n = need;
    return 1;
}

static int scan_rune_is_space(uint32_t r) {
    if (r == ' ' || r == '\t' || r == '\n' || r == '\r' ||
        r == '\v' || r == '\f')
        return 1;
    if (r == 0x85 || r == 0xA0 || r == 0x1680)
        return 1;
    if (r >= 0x2000 && r <= 0x200A)
        return 1;
    return r == 0x2028 || r == 0x2029 || r == 0x202F ||
           r == 0x205F || r == 0x3000;
}

/* Byte length of a space rune at s, or 0. allow_nl includes U+000A.
 * Invalid UTF-8 is not space: Go DecodeRune yields RuneError, and
 * isSpace(U+FFFD) is false. A lone 0xA0/0x85 byte must not skip as
 * U+00A0/U+0085 (those exist only as the 2-byte encodings C2 A0 / C2 85). */
static int scan_space_bytes(const char *s, int allow_nl) {
    if (!s || !*s) return 0;
    uint32_t r;
    int n;
    if (!scan_decode_rune(s, &r, &n))
        return 0;
    if (r == '\n') return allow_nl ? n : 0;
    return scan_rune_is_space(r) ? n : 0;
}

static int skip_ws(const char **p) {
    int total = 0, n;
    while ((n = scan_space_bytes(*p, 0)) > 0) {
        (*p) += n;
        total += n;
    }
    return total;
}

static int skip_ws_nl(const char **p) {
    int total = 0, n;
    while ((n = scan_space_bytes(*p, 1)) > 0) {
        (*p) += n;
        total += n;
    }
    return total;
}

/* Go fmt/scan.go advance(): a format newline must match an input newline
 * (or EOF). A format space matches one-or-more non-newline spaces or EOF,
 * and must not consume an input newline. */
static int scan_advance_format_space(const char **sp, const char **fp) {
    int newlines = 0;
    int trailing_space = 0;
    while (**fp) {
        int n = scan_space_bytes(*fp, 1);
        if (n == 0) break;
        if (**fp == '\n') {
            newlines++;
            trailing_space = 0;
        } else {
            trailing_space = 1;
        }
        (*fp) += n;
    }
    for (int j = 0; j < newlines; j++) {
        int n;
        while ((n = scan_space_bytes(*sp, 0)) > 0)
            (*sp) += n;
        if (**sp == '\n')
            (*sp)++;
        else if (**sp != '\0')
            return 0;
    }
    if (trailing_space) {
        if (newlines == 0) {
            if (**sp == '\n')
                return 0;
            if (**sp != '\0' && scan_space_bytes(*sp, 0) == 0)
                return 0;
        }
        int n;
        while ((n = scan_space_bytes(*sp, 0)) > 0)
            (*sp) += n;
    }
    return 1;
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
                   (*q >= 'A' && *q <= 'F') || *q == '_') {
                if (*q != '_') saw_hex = 1;
                q++;
            }
            if (*q == '.') {
                q++;
                while ((*q >= '0' && *q <= '9') ||
                       (*q >= 'a' && *q <= 'f') ||
                       (*q >= 'A' && *q <= 'F') || *q == '_') {
                    if (*q != '_') saw_hex = 1;
                    q++;
                }
            }
            if (saw_hex && (*q == 'p' || *q == 'P')) {
                const char *r = q + 1;
                if (*r == '+' || *r == '-') r++;
                if ((*r >= '0' && *r <= '9') || *r == '_') {
                    int saw_exp = 0;
                    while ((*r >= '0' && *r <= '9') || *r == '_') {
                        if (*r != '_') saw_exp = 1;
                        r++;
                    }
                    if (saw_exp) {
                        *p = r;
                        took_hex = 1;
                    }
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
            while ((**p >= '0' && **p <= '9') || **p == '_') (*p)++;
            if (**p == '.') {
                (*p)++;
                while ((**p >= '0' && **p <= '9') || **p == '_') (*p)++;
            }
            if (**p == 'e' || **p == 'E') {
                (*p)++;
                if (**p == '-' || **p == '+') (*p)++;
                while ((**p >= '0' && **p <= '9') || **p == '_')
                    (*p)++;
            }
            /* Go floatToken exponent = "eEpP": consume p/P so "2.3P2" is
             * one token. convertFloat only splits on lowercase 'p'. */
            if (**p == 'p' || **p == 'P') {
                (*p)++;
                if (**p == '-' || **p == '+') (*p)++;
                while ((**p >= '0' && **p <= '9') || **p == '_')
                    (*p)++;
            }
        }
    }

    size_t len = (size_t)(*p - start);
    char stackbuf[128];
    char *tok = (len < sizeof stackbuf) ? stackbuf : (char *)malloc(len + 1);
    if (!tok) { *p = start; return 0; }
    memcpy(tok, start, len);
    tok[len] = '\0';
    /* Match Go fmt.convertFloat: ParseFloat ErrRange (±Inf overflow) is a
     * scan failure. Explicit "Inf"/"NaN" still succeed (strconv returns OK).
     * Decimal+p is not a strconv hex float — split and ldexp like Go. */
    double parsed;
    int rc = NEVERC_STRCONV_ERR_SYNTAX;
    char *pmark = NULL;
    int has_hex = 0;
    for (size_t i = 0; i < len; i++) {
        if (tok[i] == 'x' || tok[i] == 'X') has_hex = 1;
        if (!pmark && tok[i] == 'p') pmark = tok + i;
    }
    if (pmark && !has_hex) {
        *pmark = '\0';
        double mant;
        long long exp;
        if (neverc_strconv_parse_float(tok, &mant) == NEVERC_STRCONV_OK &&
            neverc_strconv_parse_int(pmark + 1, 10, &exp) == NEVERC_STRCONV_OK &&
            exp >= INT_MIN && exp <= INT_MAX) {
            parsed = ldexp(mant, (int)exp);
            rc = NEVERC_STRCONV_OK;
        }
    } else {
        rc = neverc_strconv_parse_float(tok, &parsed);
    }
    if (tok != stackbuf) free(tok);
    if (rc != NEVERC_STRCONV_OK) { *p = start; return 0; }
    *out = parsed;
    return 1;
}

static int scan_string(const char **p, char *buf, size_t max_chars) {
    skip_ws(p);
    if (!buf || max_chars == 0 || **p == '\0') return 0;
    size_t i = 0;
    while (i < max_chars && **p && scan_space_bytes(*p, 1) == 0) {
        buf[i++] = **p;
        (*p)++;
    }
    buf[i] = '\0';
    return i > 0 ? 1 : 0;
}

static int scan_hex(const char **p, uint64_t *out) {
    skip_ws(p);
    const char *start = *p;
    /* Go %x/%X is hex digits only. A 0x prefix is %v / Scan, not %x. */
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

static int scan_oct(const char **p, uint64_t *out) {
    skip_ws(p);
    const char *start = *p;
    uint64_t val = 0;
    int found = 0;
    while (**p >= '0' && **p <= '7') {
        unsigned digit = (unsigned)(**p - '0');
        if (val > (UINT64_MAX - digit) / 8U) {
            *p = start;
            return 0;
        }
        val = val * 8U + digit;
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

static int scan_bin(const char **p, uint64_t *out) {
    skip_ws(p);
    const char *start = *p;
    uint64_t val = 0;
    int found = 0;
    while (**p == '0' || **p == '1') {
        unsigned digit = (unsigned)(**p - '0');
        if (val > (UINT64_MAX - digit) / 2U) {
            *p = start;
            return 0;
        }
        val = val * 2U + digit;
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

/* Go Scanf: SkipSpace, then at most `width` bytes of the token. */
static int scan_apply_width(const char **sp, size_t width, int has_width,
                            char *tmp, size_t cap, const char **q) {
    skip_ws(sp);
    if (!has_width) {
        *q = *sp;
        return 1;
    }
    size_t n = 0;
    while (n < width && (*sp)[n])
        n++;
    if (n >= cap)
        return 0;
    memcpy(tmp, *sp, n);
    tmp[n] = '\0';
    *q = tmp;
    return 1;
}

static void scan_commit_width(const char **sp, const char *q, const char *tmp,
                              int has_width) {
    if (has_width)
        *sp += (size_t)(q - tmp);
    else
        *sp = q;
}

/* Go unformatted Scan/Sscan: 0x/0b/0o/leading-0 prefixes and underscores.
 * Formatted %d stays decimal-only (scan_int) — Go %d does not take those. */
static int scan_int_literal(const char **p, int64_t *out) {
    skip_ws_nl(p);
    const char *start = *p;
    if (**p == '+' || **p == '-') (*p)++;
    if (**p < '0' || **p > '9') {
        *p = start;
        return 0;
    }

    const char *q = *p;
    const char *digits = "0123456789";
    if (*q == '0' && (q[1] == 'x' || q[1] == 'X')) {
        q += 2;
        digits = "0123456789abcdefABCDEF";
    } else if (*q == '0' && (q[1] == 'b' || q[1] == 'B')) {
        q += 2;
        digits = "01";
    } else if (*q == '0' && (q[1] == 'o' || q[1] == 'O')) {
        q += 2;
        digits = "01234567";
    } else if (*q == '0') {
        /* Go scanBasePrefix: a leading 0 that is not 0x/0b/0o is octal.
         * "08" therefore scans as 0 with leftover '8', not a failed token. */
        digits = "01234567";
    }
    while (*q) {
        if (*q != '_' && !strchr(digits, *q))
            break;
        q++;
    }

    size_t len = (size_t)(q - start);
    if (len == 0) {
        *p = start;
        return 0;
    }
    char stacktok[128];
    char *tok = stacktok;
    if (len >= sizeof(stacktok)) {
        tok = (char *)malloc(len + 1);
        if (!tok) {
            *p = start;
            return 0;
        }
    }
    memcpy(tok, start, len);
    tok[len] = '\0';
    long long parsed;
    int ok = neverc_strconv_parse_int(tok, 0, &parsed) == NEVERC_STRCONV_OK;
    if (tok != stacktok) free(tok);
    if (!ok) {
        *p = start;
        return 0;
    }
    *p = q;
    *out = (int64_t)parsed;
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
            if (scan_space_bytes(fp, 1) > 0) {
                if (!scan_advance_format_space(&sp, &fp))
                    break;
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
            /* Go scanPercent: SkipSpace before matching a literal '%'. */
            skip_ws(&sp);
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
            char tmp[256];
            const char *q;
            if (!scan_apply_width(&sp, width, has_width, tmp, sizeof tmp, &q) ||
                !scan_int(&q, &value))
                goto done;
            scan_commit_width(&sp, q, tmp, has_width);
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
        case 'X':
        case 'o':
        case 'b': {
            uint64_t value;
            char tmp[256];
            const char *q;
            int ok;
            if (!scan_apply_width(&sp, width, has_width, tmp, sizeof tmp, &q))
                goto done;
            if (*fp == 'u')
                ok = scan_uint(&q, &value);
            else if (*fp == 'o')
                ok = scan_oct(&q, &value);
            else if (*fp == 'b')
                ok = scan_bin(&q, &value);
            else
                ok = scan_hex(&q, &value);
            if (!ok)
                goto done;
            scan_commit_width(&sp, q, tmp, has_width);
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
        case 'F':
        case 'g':
        case 'G':
        case 'e':
        case 'E': {
            double value;
            char tmp[256];
            const char *q;
            if (length == SCAN_LENGTH_LONG_LONG ||
                !scan_apply_width(&sp, width, has_width, tmp, sizeof tmp, &q) ||
                !scan_float(&q, &value))
                goto done;
            scan_commit_width(&sp, q, tmp, has_width);
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
        if (!scan_int_literal(&sp, &value) || !scan_value_fits_int(value))
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
    if (scan_int_literal(&p, &val) && scan_value_fits_int(val)) {
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

static int fmt_copied_len(size_t copy) {
    return copy > (size_t)INT_MAX ? INT_MAX : (int)copy;
}

int neverc_fmt_appendf(char *buf, size_t cap, const char *format, ...) {
    size_t existing;
    if (!format || !append_position(buf, cap, &existing)) return 0;

    va_list args;
    va_start(args, format);
    size_t slen = 0;
    char *s = fmt_vsprintf_n(format, args, &slen);
    va_end(args);
    if (!s) return 0;
    size_t space = cap - existing - 1;
    size_t copy = slen < space ? slen : space;
    for (size_t i = 0; i < copy; i++) buf[existing + i] = s[i];
    buf[existing + copy] = '\0';
    free(s);
    return fmt_copied_len(copy);
}

int neverc_fmt_append(char *buf, size_t cap, const char *s) {
    size_t existing;
    if (!s || !append_position(buf, cap, &existing)) return 0;
    size_t slen = my_strlen(s);
    size_t space = cap - existing - 1;
    size_t copy = slen < space ? slen : space;
    for (size_t i = 0; i < copy; i++) buf[existing + i] = s[i];
    buf[existing + copy] = '\0';
    return fmt_copied_len(copy);
}

int neverc_fmt_appendln(char *buf, size_t cap, const char *s) {
    size_t existing;
    if (!s || !append_position(buf, cap, &existing)) return 0;
    size_t slen = my_strlen(s);
    size_t space = cap - existing - 1;
    if (slen == SIZE_MAX) return 0;
    size_t need = slen + 1;
    size_t copy = need < space ? need : space;
    size_t scopy = copy > 0 ? (copy > slen ? slen : copy) : 0;
    for (size_t i = 0; i < scopy; i++) buf[existing + i] = s[i];
    if (scopy < copy) buf[existing + scopy] = '\n';
    buf[existing + copy] = '\0';
    return fmt_copied_len(copy);
}

char *neverc_fmt_sprintfln(const char *format, ...) {
    va_list args;
    va_start(args, format);
    size_t len = 0;
    char *s = fmt_vsprintf_n(format, args, &len);
    va_end(args);
    if (!s) return NULL;
    if (len > SIZE_MAX - 2) { free(s); return NULL; }
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
    if (scan_int_literal(&p, &val) && scan_value_fits_int(val)) {
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
