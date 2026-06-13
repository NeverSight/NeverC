#include "neverc/std/strconv.h"
#include "neverc/std/unicode/utf8.h"
#include "neverc/std/unicode.h"
#include <stdlib.h>
#include <string.h>

static const char lowerhex[] = "0123456789abcdef";

static int is_print_rune(uint32_t r) {
    if (r <= 0x7F) {
        if (r >= 0x20 && r <= 0x7E) return 1;
        return 0;
    }
    return neverc_unicode_is_print(r);
}

static int is_graphic_rune(uint32_t r) {
    if (is_print_rune(r)) return 1;
    return neverc_unicode_is_graphic(r);
}

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} strbuf_t;

static void buf_init(strbuf_t *b, size_t initial) {
    b->cap = initial < 64 ? 64 : initial;
    b->data = (char *)malloc(b->cap);
    b->len = 0;
}

static void buf_grow(strbuf_t *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        size_t newcap = b->cap * 2;
        if (newcap < b->len + extra + 1)
            newcap = b->len + extra + 1;
        b->data = (char *)realloc(b->data, newcap);
        b->cap = newcap;
    }
}

static void buf_putc(strbuf_t *b, char c) {
    buf_grow(b, 1);
    b->data[b->len++] = c;
}

static void buf_puts(strbuf_t *b, const char *s, size_t n) {
    buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

static char *buf_finish(strbuf_t *b) {
    buf_grow(b, 1);
    b->data[b->len] = '\0';
    return b->data;
}

static void append_escaped_rune(strbuf_t *b, uint32_t r, char quote,
                                int ascii_only, int graphic_only) {
    if (r == (uint32_t)quote || r == '\\') {
        buf_putc(b, '\\');
        buf_putc(b, (char)r);
        return;
    }
    if (ascii_only) {
        if (r < NEVERC_UTF8_RUNE_SELF && is_print_rune(r)) {
            buf_putc(b, (char)r);
            return;
        }
    } else if (is_print_rune(r) || (graphic_only && is_graphic_rune(r))) {
        uint8_t enc[4];
        int n = neverc_utf8_encode_rune(enc, r);
        buf_puts(b, (const char *)enc, (size_t)n);
        return;
    }
    switch (r) {
    case '\a': buf_puts(b, "\\a", 2); return;
    case '\b': buf_puts(b, "\\b", 2); return;
    case '\f': buf_puts(b, "\\f", 2); return;
    case '\n': buf_puts(b, "\\n", 2); return;
    case '\r': buf_puts(b, "\\r", 2); return;
    case '\t': buf_puts(b, "\\t", 2); return;
    case '\v': buf_puts(b, "\\v", 2); return;
    }
    if (r < ' ' || r == 0x7F) {
        buf_puts(b, "\\x", 2);
        buf_putc(b, lowerhex[(r >> 4) & 0xF]);
        buf_putc(b, lowerhex[r & 0xF]);
    } else if (!neverc_utf8_valid_rune(r)) {
        buf_puts(b, "\\ufffd", 6);
    } else if (r < 0x10000) {
        buf_puts(b, "\\u", 2);
        buf_putc(b, lowerhex[(r >> 12) & 0xF]);
        buf_putc(b, lowerhex[(r >> 8) & 0xF]);
        buf_putc(b, lowerhex[(r >> 4) & 0xF]);
        buf_putc(b, lowerhex[r & 0xF]);
    } else {
        buf_puts(b, "\\U", 2);
        buf_putc(b, lowerhex[(r >> 28) & 0xF]);
        buf_putc(b, lowerhex[(r >> 24) & 0xF]);
        buf_putc(b, lowerhex[(r >> 20) & 0xF]);
        buf_putc(b, lowerhex[(r >> 16) & 0xF]);
        buf_putc(b, lowerhex[(r >> 12) & 0xF]);
        buf_putc(b, lowerhex[(r >> 8) & 0xF]);
        buf_putc(b, lowerhex[(r >> 4) & 0xF]);
        buf_putc(b, lowerhex[r & 0xF]);
    }
}

static char *quote_with(const char *s, char quote, int ascii_only, int graphic_only) {
    size_t slen = strlen(s);
    strbuf_t b;
    buf_init(&b, slen + 2);
    buf_putc(&b, quote);

    size_t i = 0;
    while (i < slen) {
        /* Fast path: bulk-copy a run of printable ASCII bytes that quote to
         * themselves (everything in 0x20..0x7E except the quote char and '\').
         * These are emitted verbatim in every mode, so this avoids per-byte
         * rune decoding and per-character buffer appends for typical text. */
        unsigned char c0 = (unsigned char)s[i];
        if (c0 >= 0x20 && c0 <= 0x7E && c0 != (unsigned char)quote && c0 != '\\') {
            size_t start = i;
            do {
                i++;
                c0 = (unsigned char)s[i];
            } while (i < slen && c0 >= 0x20 && c0 <= 0x7E &&
                     c0 != (unsigned char)quote && c0 != '\\');
            buf_puts(&b, s + start, i - start);
            continue;
        }
        uint32_t r;
        int width;
        neverc_utf8_decode_rune((const uint8_t *)s + i, slen - i, &r, &width);
        if (width == 1 && r == NEVERC_UTF8_RUNE_ERROR) {
            buf_puts(&b, "\\x", 2);
            buf_putc(&b, lowerhex[((uint8_t)s[i] >> 4) & 0xF]);
            buf_putc(&b, lowerhex[(uint8_t)s[i] & 0xF]);
            i++;
            continue;
        }
        append_escaped_rune(&b, r, quote, ascii_only, graphic_only);
        i += (size_t)width;
    }
    buf_putc(&b, quote);
    return buf_finish(&b);
}

static char *quote_rune_with(uint32_t r, char quote, int ascii_only, int graphic_only) {
    strbuf_t b;
    buf_init(&b, 12);
    buf_putc(&b, quote);
    if (!neverc_utf8_valid_rune(r))
        r = NEVERC_UTF8_RUNE_ERROR;
    append_escaped_rune(&b, r, quote, ascii_only, graphic_only);
    buf_putc(&b, quote);
    return buf_finish(&b);
}

char *neverc_strconv_quote(const char *s) {
    return quote_with(s, '"', 0, 0);
}

char *neverc_strconv_quote_to_ascii(const char *s) {
    return quote_with(s, '"', 1, 0);
}

char *neverc_strconv_quote_to_graphic(const char *s) {
    return quote_with(s, '"', 0, 1);
}

char *neverc_strconv_quote_rune(uint32_t r) {
    return quote_rune_with(r, '\'', 0, 0);
}

char *neverc_strconv_quote_rune_to_ascii(uint32_t r) {
    return quote_rune_with(r, '\'', 1, 0);
}

char *neverc_strconv_quote_rune_to_graphic(uint32_t r) {
    return quote_rune_with(r, '\'', 0, 1);
}

int neverc_strconv_can_backquote(const char *s) {
    size_t slen = strlen(s);
    size_t i = 0;
    while (i < slen) {
        uint32_t r;
        int width;
        neverc_utf8_decode_rune((const uint8_t *)s + i, slen - i, &r, &width);
        if (width > 1) {
            if (r == 0xFEFF) return 0;
            i += (size_t)width;
            continue;
        }
        if (r == NEVERC_UTF8_RUNE_ERROR) return 0;
        if ((r < ' ' && r != '\t') || r == '`' || r == 0x7F)
            return 0;
        i++;
    }
    return 1;
}

static int unhex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int neverc_strconv_unquote_char(const char *s, size_t slen, char quote,
                                uint32_t *r, int *multibyte) {
    if (!s || slen == 0) return -1;
    *multibyte = 0;

    unsigned char c = (unsigned char)s[0];
    if (c == (unsigned char)quote && (quote == '\'' || quote == '"'))
        return -1;

    if (c >= NEVERC_UTF8_RUNE_SELF) {
        int width;
        neverc_utf8_decode_rune((const uint8_t *)s, slen, r, &width);
        *multibyte = 1;
        return width;
    }
    if (c != '\\') {
        *r = c;
        return 1;
    }

    if (slen <= 1) return -1;
    c = (unsigned char)s[1];
    int consumed = 2;

    switch (c) {
    case 'a': *r = '\a'; break;
    case 'b': *r = '\b'; break;
    case 'f': *r = '\f'; break;
    case 'n': *r = '\n'; break;
    case 'r': *r = '\r'; break;
    case 't': *r = '\t'; break;
    case 'v': *r = '\v'; break;
    case '\\': *r = '\\'; break;
    case '\'': case '"':
        if (c != (unsigned char)quote) return -1;
        *r = c;
        break;
    case 'x': case 'u': case 'U': {
        int n = (c == 'x') ? 2 : (c == 'u') ? 4 : 8;
        if ((size_t)(2 + n) > slen) return -1;
        uint32_t v = 0;
        for (int j = 0; j < n; j++) {
            int h = unhex(s[2 + j]);
            if (h < 0) return -1;
            v = (v << 4) | (uint32_t)h;
        }
        if (c == 'x') {
            *r = v;
        } else {
            if (!neverc_utf8_valid_rune(v)) return -1;
            *r = v;
            *multibyte = 1;
        }
        consumed = 2 + n;
        break;
    }
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7': {
        uint32_t v = (uint32_t)(c - '0');
        if (slen < 4) return -1;
        for (int j = 0; j < 2; j++) {
            int d = (unsigned char)s[2 + j] - '0';
            if (d < 0 || d > 7) return -1;
            v = (v << 3) | (uint32_t)d;
        }
        if (v > 255) return -1;
        *r = v;
        consumed = 4;
        break;
    }
    default:
        return -1;
    }
    return consumed;
}

int neverc_strconv_unquote(const char *s, char *buf, size_t bufsize) {
    if (!s || !buf || bufsize == 0) return -1;
    size_t slen = strlen(s);
    if (slen < 2) return -1;

    char quote = s[0];
    if ((quote != '"' && quote != '\'' && quote != '`') || s[slen - 1] != quote)
        return -1;

    if (quote == '`') {
        size_t inner = slen - 2;
        if (inner + 1 > bufsize) return -1;
        memcpy(buf, s + 1, inner);
        buf[inner] = '\0';
        return (int)inner;
    }

    const char *src = s + 1;
    size_t src_len = slen - 2;
    size_t out = 0;

    while (src_len > 0) {
        uint32_t r;
        int mb;
        int consumed = neverc_strconv_unquote_char(src, src_len, quote, &r, &mb);
        if (consumed < 0) return -1;

        uint8_t enc[4];
        int n = neverc_utf8_encode_rune(enc, r);
        if (out + (size_t)n >= bufsize) return -1;
        memcpy(buf + out, enc, (size_t)n);
        out += (size_t)n;

        src += consumed;
        src_len -= (size_t)consumed;
    }
    buf[out] = '\0';
    return (int)out;
}

int neverc_strconv_is_print(uint32_t r) {
    return is_print_rune(r);
}

int neverc_strconv_is_graphic(uint32_t r) {
    return is_graphic_rune(r);
}

static int append_quoted_helper(char *buf, size_t cap, char *q) {
    if (!q) return -1;
    size_t len = strlen(q);
    if (len + 1 > cap) { free(q); return -1; }
    memcpy(buf, q, len + 1);
    free(q);
    return (int)len;
}

int neverc_strconv_append_quote(char *buf, size_t cap, const char *s) {
    return append_quoted_helper(buf, cap, neverc_strconv_quote(s));
}

int neverc_strconv_append_quote_to_ascii(char *buf, size_t cap, const char *s) {
    return append_quoted_helper(buf, cap, neverc_strconv_quote_to_ascii(s));
}

int neverc_strconv_append_quote_to_graphic(char *buf, size_t cap, const char *s) {
    return append_quoted_helper(buf, cap, neverc_strconv_quote_to_graphic(s));
}

int neverc_strconv_append_quote_rune(char *buf, size_t cap, uint32_t r) {
    return append_quoted_helper(buf, cap, neverc_strconv_quote_rune(r));
}

int neverc_strconv_append_quote_rune_to_ascii(char *buf, size_t cap, uint32_t r) {
    return append_quoted_helper(buf, cap, neverc_strconv_quote_rune_to_ascii(r));
}

int neverc_strconv_append_quote_rune_to_graphic(char *buf, size_t cap, uint32_t r) {
    return append_quoted_helper(buf, cap, neverc_strconv_quote_rune_to_graphic(r));
}

int neverc_strconv_append_bool(char *buf, size_t cap, int b) {
    return neverc_strconv_format_bool(b, buf, cap);
}

int neverc_strconv_append_int(char *buf, size_t cap, long long n, int base) {
    return neverc_strconv_format_int(n, base, buf, cap);
}

int neverc_strconv_append_uint(char *buf, size_t cap, unsigned long long n, int base) {
    return neverc_strconv_format_uint(n, base, buf, cap);
}

int neverc_strconv_append_float(char *buf, size_t cap, double f, char fmt, int prec) {
    return neverc_strconv_format_float(f, fmt, prec, buf, cap);
}

int neverc_strconv_quoted_prefix(const char *s, size_t *prefix_len) {
    if (!s || !prefix_len) return -1;
    size_t slen = strlen(s);
    if (slen == 0) return -1;

    char quote = s[0];
    if (quote == '`') {
        for (size_t i = 1; i < slen; i++) {
            if (s[i] == '`') {
                *prefix_len = i + 1;
                return 0;
            }
        }
        return -1;
    }
    if (quote != '"' && quote != '\'') return -1;

    size_t i = 1;
    while (i < slen) {
        if (s[i] == quote) {
            *prefix_len = i + 1;
            return 0;
        }
        if (s[i] == '\\') {
            uint32_t r;
            int mb;
            int consumed = neverc_strconv_unquote_char(s + i, slen - i, quote, &r, &mb);
            if (consumed < 0) return -1;
            i += (size_t)consumed;
        } else {
            uint32_t r;
            int width;
            neverc_utf8_decode_rune((const uint8_t *)s + i, slen - i, &r, &width);
            if (width == 1 && r == NEVERC_UTF8_RUNE_ERROR) return -1;
            i += (size_t)width;
        }
    }
    return -1;
}

int neverc_strconv_format_complex(double re, double im, char fmt, int prec,
                                   char *buf, size_t bufsize) {
    if (!buf || bufsize < 8) return -1;
    char re_buf[64], im_buf[64];
    int re_len = neverc_strconv_format_float(re, fmt, prec, re_buf, sizeof(re_buf));
    int im_len = neverc_strconv_format_float(im, fmt, prec, im_buf, sizeof(im_buf));
    if (re_len < 0 || im_len < 0) return -1;

    size_t total = 1 + (size_t)re_len + (size_t)im_len + 2;
    if (total + 1 > bufsize) return -1;

    size_t pos = 0;
    buf[pos++] = '(';
    memcpy(buf + pos, re_buf, (size_t)re_len); pos += (size_t)re_len;
    if (im >= 0 || (im != im)) buf[pos++] = '+';
    memcpy(buf + pos, im_buf, (size_t)im_len); pos += (size_t)im_len;
    buf[pos++] = 'i';
    buf[pos++] = ')';
    buf[pos] = '\0';
    return (int)pos;
}

int neverc_strconv_parse_complex(const char *s, double *re, double *im) {
    if (!s || !re || !im) return NEVERC_STRCONV_ERR_SYNTAX;
    size_t slen = strlen(s);
    if (slen < 3) return NEVERC_STRCONV_ERR_SYNTAX;

    const char *start = s;
    const char *end = s + slen;
    if (*start == '(' && *(end - 1) == ')') { start++; end--; slen -= 2; }
    if (slen == 0 || *(end - 1) != 'i') return NEVERC_STRCONV_ERR_SYNTAX;
    end--;

    const char *split = NULL;
    for (const char *p = end - 1; p > start; p--) {
        if (*p == '+' || *p == '-') {
            if (p > start && (*(p-1) == 'e' || *(p-1) == 'E')) continue;
            split = p;
            break;
        }
    }
    if (!split) return NEVERC_STRCONV_ERR_SYNTAX;

    char re_str[128], im_str[128];
    size_t re_len = (size_t)(split - start);
    size_t im_len = (size_t)(end - split);
    if (re_len >= sizeof(re_str) || im_len >= sizeof(im_str))
        return NEVERC_STRCONV_ERR_SYNTAX;

    memcpy(re_str, start, re_len); re_str[re_len] = '\0';
    memcpy(im_str, split, im_len); im_str[im_len] = '\0';

    int r1 = neverc_strconv_parse_float(re_str, re);
    int r2 = neverc_strconv_parse_float(im_str, im);
    if (r1 != 0) return r1;
    if (r2 != 0) return r2;
    return 0;
}
