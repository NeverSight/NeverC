#include "neverc/std/text/scanner.h"
#include "neverc/std/unicode.h"
#include "neverc/std/unicode/utf8.h"
#include <limits.h>
#include <string.h>

#define NCI_SCANNER_OVERFLOW UINT32_C(0x80000000)
#define NCI_SCANNER_ERROR_MASK UINT32_C(0x7fffffff)

/* v3389 exposed the scanner by value. Every shipped target is 64-bit, where
 * tok_len's 8-byte alignment leaves four ABI-private bytes immediately after
 * tok_buf. mode is followed directly by tok_pos and has no padding. memcpy
 * avoids creating an unaligned or aliased uint32_t object. */
#define NCI_SCANNER_STATE_OFFSET                                           \
    (offsetof(neverc_scanner_t, tok_buf) +                                \
     sizeof(((neverc_scanner_t *)0)->tok_buf))

_Static_assert(sizeof(size_t) == 8,
               "32-bit scanner needs out-of-struct private state");
_Static_assert(offsetof(neverc_scanner_t, tok_pos) ==
                   offsetof(neverc_scanner_t, mode) +
                       sizeof(((neverc_scanner_t *)0)->mode),
               "neverc_scanner_t unexpectedly has padding after mode");
_Static_assert(offsetof(neverc_scanner_t, tok_len) ==
                   NCI_SCANNER_STATE_OFFSET + sizeof(uint32_t),
               "neverc_scanner_t lost its 64-bit ABI-private padding");

static uint32_t scanner_state_load(const neverc_scanner_t *s) {
    uint32_t state = 0;
    memcpy(&state, (const unsigned char *)s + NCI_SCANNER_STATE_OFFSET,
           sizeof(state));
    return state;
}

static void scanner_state_store(neverc_scanner_t *s, uint32_t state) {
    memcpy((unsigned char *)s + NCI_SCANNER_STATE_OFFSET, &state,
           sizeof(state));
}

static int scanner_error_count_value(const neverc_scanner_t *s) {
    return (int)(scanner_state_load(s) & NCI_SCANNER_ERROR_MASK);
}

static void scanner_add_error(neverc_scanner_t *s) {
    uint32_t state = scanner_state_load(s);
    uint32_t errors = state & NCI_SCANNER_ERROR_MASK;
    if (errors != NCI_SCANNER_ERROR_MASK) errors++;
    scanner_state_store(s, (state & NCI_SCANNER_OVERFLOW) | errors);
}

static int scanner_token_overflow(const neverc_scanner_t *s) {
    return (scanner_state_load(s) & NCI_SCANNER_OVERFLOW) != 0;
}

static void scanner_set_token_overflow(neverc_scanner_t *s, int overflow) {
    uint32_t state = scanner_state_load(s) & NCI_SCANNER_ERROR_MASK;
    if (overflow) state |= NCI_SCANNER_OVERFLOW;
    scanner_state_store(s, state);
}

static int is_digit(int ch) {
    return ch >= '0' && ch <= '9';
}

/* Go text/scanner's default identifier predicate. The first rune must be an
 * underscore or Unicode letter; Unicode digits are accepted thereafter. */
static int is_ident_rune(int ch, size_t index) {
    if (ch < 0) return 0;
    uint32_t rune = (uint32_t)ch;
    return rune == '_' || neverc_unicode_is_letter(rune) ||
           (index > 0 && neverc_unicode_is_digit(rune));
}

static int is_hex_digit(int ch) {
    return is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static int is_oct_digit(int ch) {
    return ch >= '0' && ch <= '7';
}

/*
 * Identifier-continuation lookup: 1 for letter, digit or '_' (i.e. the bytes
 * accepted after the first identifier rune). Lets scan_identifier locate the
 * end of a run in a tight branch-light loop instead of two helper calls per
 * byte, so the whole run can be copied with one memcpy.
 */
static const unsigned char nci_ident_char[256] = {
    /* 0x00 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0x10 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0x20 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0x30 */ 1,1,1,1,1,1,1,1, 1,1,0,0,0,0,0,0,  /* 0-9 */
    /* 0x40 */ 0,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,  /* A-O */
    /* 0x50 */ 1,1,1,1,1,1,1,1, 1,1,1,0,0,0,0,1,  /* P-Z, '_' */
    /* 0x60 */ 0,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,  /* a-o */
    /* 0x70 */ 1,1,1,1,1,1,1,1, 1,1,1,0,0,0,0,0,  /* p-z */
    /* 0x80 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0x90 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xA0 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xB0 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xC0 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xD0 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xE0 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    /* 0xF0 */ 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
};

static int pos_offset(size_t pos) {
    return pos > (size_t)INT_MAX ? INT_MAX : (int)pos;
}

static void add_col(neverc_scanner_t *s, size_t n) {
    if (n == 0) return;
    if (s->col < 1) s->col = 1;
    if (n > (size_t)INT_MAX || s->col > INT_MAX - (int)n)
        s->col = INT_MAX;
    else
        s->col += (int)n;
}

static int peek_rune(const neverc_scanner_t *s, size_t *width) {
    if (width) *width = 0;
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;

    unsigned char first = (unsigned char)s->src[s->pos];
    if (first < NEVERC_UTF8_RUNE_SELF) {
        if (width) *width = 1;
        return (int)first;
    }

    uint32_t rune;
    int decoded_width;
    neverc_utf8_decode_rune((const uint8_t *)s->src + s->pos,
                            s->src_len - s->pos, &rune, &decoded_width);
    if (decoded_width <= 0)
        decoded_width = 1;
    if (width) *width = (size_t)decoded_width;
    return (int)rune;
}

static int peek_ch(neverc_scanner_t *s) {
    return peek_rune(s, NULL);
}

static int next_ch(neverc_scanner_t *s) {
    size_t width;
    int ch = peek_rune(s, &width);
    if (ch == NEVERC_SCANNER_EOF) return ch;
    s->pos += width;
    if (ch == NEVERC_UTF8_RUNE_ERROR && width == 1)
        scanner_add_error(s);
    if (ch == '\n') {
        if (s->line < INT_MAX) s->line++;
        s->col = 1;
    } else {
        add_col(s, 1);
    }
    return ch;
}

static void emit_bytes(neverc_scanner_t *s, const char *data, size_t len) {
    size_t cap = sizeof(s->tok_buf) - 1;
    size_t space = s->tok_len < cap ? cap - s->tok_len : 0;
    size_t copy = len < space ? len : space;
    if (copy > 0) {
        memcpy(s->tok_buf + s->tok_len, data, copy);
        s->tok_len += copy;
    }
    if (copy < len)
        scanner_set_token_overflow(s, 1);
}

/* ASCII-only scanner paths use emit(); rune paths copy source bytes with
 * emit_bytes() so TokenText remains byte-exact even for invalid UTF-8. */
static void emit(neverc_scanner_t *s, int ch) {
    char byte = (char)ch;
    emit_bytes(s, &byte, 1);
}

static void emit_consumed(neverc_scanner_t *s, size_t start) {
    if (s->pos > start)
        emit_bytes(s, s->src + start, s->pos - start);
}

static int is_whitespace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static void skip_whitespace(neverc_scanner_t *s) {
    while (s->pos < s->src_len && is_whitespace(peek_ch(s)))
        next_ch(s);
}

/* True when Scan would treat the next bytes as a comment token. */
static int at_comment(const neverc_scanner_t *s) {
    if (s->pos >= s->src_len || (unsigned char)s->src[s->pos] != '/')
        return 0;
    if (s->pos + 1 >= s->src_len) return 0;
    unsigned char n = (unsigned char)s->src[s->pos + 1];
    return n == '/' || n == '*';
}

/* Advance past one line (//) or block (slash-star) comment without touching tok_buf. */
static void skip_one_comment(neverc_scanner_t *s) {
    next_ch(s); /* '/' */
    int second = next_ch(s);
    if (second == '/') {
        while (peek_ch(s) != NEVERC_SCANNER_EOF && peek_ch(s) != '\n')
            next_ch(s);
    } else if (second == '*') {
        while (s->pos < s->src_len) {
            int ch = next_ch(s);
            if (ch == '*' && peek_ch(s) == '/') {
                next_ch(s);
                break;
            }
        }
    }
}

static int scan_identifier(neverc_scanner_t *s) {
    const char *src = s->src;
    size_t len = s->src_len;
    size_t rune_index = 1; /* The caller already consumed rune zero. */

    while (s->pos < len) {
        /* Preserve the old branch-light ASCII fast path. */
        size_t start = s->pos;
        size_t i = start;
        while (i < len && (unsigned char)src[i] < 0x80 &&
               nci_ident_char[(unsigned char)src[i]])
            i++;
        if (i > start) {
            size_t run = i - start;
            emit_bytes(s, src + start, run);
            s->pos = i;
            add_col(s, run);
            rune_index += run;
            continue;
        }

        int ch = peek_ch(s);
        if (!is_ident_rune(ch, rune_index))
            break;
        start = s->pos;
        next_ch(s);
        emit_consumed(s, start);
        rune_index++;
    }
    return NEVERC_SCANNER_IDENT;
}

/* Go text/scanner digits(): '_' and, for base<=10, every decimal digit stays
 * in the token. Record the first digit outside the radix when requested. */
static void scan_digits(neverc_scanner_t *s, int base, int *invalid_digit) {
    for (;;) {
        int ch = peek_ch(s);
        if (ch == NEVERC_SCANNER_EOF) return;
        if (ch == '_') {
            emit(s, next_ch(s));
            continue;
        }
        if (base > 10) {
            if (!is_hex_digit(ch)) return;
        } else {
            if (!is_digit(ch)) return;
            if (invalid_digit && *invalid_digit == 0 && ch >= '0' + base)
                *invalid_digit = ch;
        }
        emit(s, next_ch(s));
    }
}

static int scan_number(neverc_scanner_t *s, int first) {
    int is_float = (first == '.');
    int base = 10;
    int invalid_digit = 0;
    emit(s, first);

    if (!is_float && first == '0' && s->pos < s->src_len) {
        int ch = peek_ch(s);
        if (ch == 'x' || ch == 'X') {
            emit(s, next_ch(s));
            base = 16;
        } else if (ch == 'o' || ch == 'O') {
            emit(s, next_ch(s));
            base = 8;
        } else if (ch == 'b' || ch == 'B') {
            emit(s, next_ch(s));
            base = 2;
        } else {
            base = 8;
        }
    }

    if (!is_float)
        scan_digits(s, base, &invalid_digit);

    /* Go: '.' after the mantissa starts a float whenever ScanFloats is set,
     * including 0b/0o prefixes and with no fractional digits ("1.", "0b1.0"). */
    if ((s->mode & NEVERC_SCAN_FLOATS) && s->pos < s->src_len &&
        peek_ch(s) == '.') {
        is_float = 1;
        emit(s, next_ch(s));
    }

    if (is_float)
        scan_digits(s, base, NULL);

    /* Go accepts e/E and p/P exponents under ScanFloats for every prefix
     * (invalid combinations are still one Float token). */
    if ((s->mode & NEVERC_SCAN_FLOATS) && s->pos < s->src_len) {
        int ch = peek_ch(s);
        if (ch == 'e' || ch == 'E' || ch == 'p' || ch == 'P') {
            is_float = 1;
            emit(s, next_ch(s));
            if (s->pos < s->src_len && (peek_ch(s) == '+' || peek_ch(s) == '-'))
                emit(s, next_ch(s));
            scan_digits(s, 10, NULL);
        }
    }

    if (!is_float && invalid_digit != 0)
        scanner_add_error(s);
    return is_float ? NEVERC_SCANNER_FLOAT : NEVERC_SCANNER_INT;
}

/* Returns the byte after '\\'. A source newline is not part of the literal
 * (Go: "literal not terminated") and must not be emitted into tok_buf. */
static int scan_escape(neverc_scanner_t *s, int quote) {
    size_t start = s->pos;
    int ch = next_ch(s);
    if (ch == NEVERC_SCANNER_EOF || ch == '\n') return ch;
    emit_consumed(s, start);
    if (ch == 'x') {
        int digits = 0;
        for (; digits < 2 && s->pos < s->src_len && is_hex_digit(peek_ch(s)); digits++)
            emit(s, next_ch(s));
        if (digits != 2) scanner_add_error(s);
    } else if (ch == 'u') {
        int digits = 0;
        for (; digits < 4 && s->pos < s->src_len && is_hex_digit(peek_ch(s)); digits++)
            emit(s, next_ch(s));
        if (digits != 4) scanner_add_error(s);
    } else if (ch == 'U') {
        int digits = 0;
        for (; digits < 8 && s->pos < s->src_len && is_hex_digit(peek_ch(s)); digits++)
            emit(s, next_ch(s));
        if (digits != 8) scanner_add_error(s);
    } else if (is_oct_digit(ch)) {
        int digits = 1;
        for (; digits < 3 && s->pos < s->src_len && is_oct_digit(peek_ch(s)); digits++)
            emit(s, next_ch(s));
        if (digits != 3) scanner_add_error(s);
    } else {
        switch (ch) {
        case 'a': case 'b': case 'f': case 'n':
        case 'r': case 't': case 'v': case '\\':
            break;
        default:
            if (ch != quote) scanner_add_error(s);
            break;
        }
    }
    return ch;
}

static int scan_string(neverc_scanner_t *s, int quote) {
    int terminated = 0;
    emit(s, quote);
    while (s->pos < s->src_len) {
        size_t start = s->pos;
        int ch = next_ch(s);
        if (ch == quote) {
            emit_consumed(s, start);
            terminated = 1;
            break;
        }
        if (ch == '\\') {
            emit_consumed(s, start);
            if (scan_escape(s, quote) == '\n') break;
            continue;
        }
        if (ch == '\n') break;
        emit_consumed(s, start);
    }
    if (!terminated) scanner_add_error(s);
    return (quote == '\'') ? NEVERC_SCANNER_CHAR : NEVERC_SCANNER_STRING;
}

static int scan_raw_string(neverc_scanner_t *s) {
    int terminated = 0;
    emit(s, '`');
    while (s->pos < s->src_len) {
        size_t start = s->pos;
        int ch = next_ch(s);
        emit_consumed(s, start);
        if (ch == '`') {
            terminated = 1;
            break;
        }
    }
    if (!terminated) scanner_add_error(s);
    return NEVERC_SCANNER_RAWSTRING;
}

static int scan_comment(neverc_scanner_t *s, int second) {
    emit(s, '/');
    emit(s, second);
    if (second == '/') {
        while (s->pos < s->src_len && peek_ch(s) != '\n') {
            /* Preserve the common ASCII comment fast path while falling back
             * to rune decoding for non-ASCII text so columns stay rune-based. */
            size_t start = s->pos;
            size_t i = start;
            while (i < s->src_len &&
                   (unsigned char)s->src[i] < NEVERC_UTF8_RUNE_SELF &&
                   s->src[i] != '\n')
                i++;
            if (i > start) {
                emit_bytes(s, s->src + start, i - start);
                s->pos = i;
                add_col(s, i - start);
                continue;
            }
            next_ch(s);
            emit_consumed(s, start);
        }
    } else {
        /* C/Go block comments are not nested: the first * / ends the comment. */
        int terminated = 0;
        while (s->pos < s->src_len) {
            size_t start = s->pos;
            int ch = next_ch(s);
            emit_consumed(s, start);
            if (ch == '*' && s->pos < s->src_len && peek_ch(s) == '/') {
                start = s->pos;
                next_ch(s);
                emit_consumed(s, start);
                terminated = 1;
                break;
            }
        }
        if (!terminated) scanner_add_error(s);
    }
    return NEVERC_SCANNER_COMMENT;
}

void neverc_scanner_init(neverc_scanner_t *s, const char *src, size_t len) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (!src) {
        src = "";
        len = 0;
    }
    s->src = src;
    s->src_len = len;
    s->mode = NEVERC_SCAN_GO_TOKENS;
    s->line = 1;
    s->col = 1;
    /* Go text/scanner: a leading UTF-8 BOM is discarded, not tokenized. */
    if (len >= 3 &&
        (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF)
        s->pos = 3;
}

void neverc_scanner_set_mode(neverc_scanner_t *s, unsigned mode) {
    if (!s) return;
    s->mode = mode;
}

int neverc_scanner_scan(neverc_scanner_t *s) {
    if (!s) return NEVERC_SCANNER_EOF;
    s->tok_len = 0;
    scanner_set_token_overflow(s, 0);

again:
    skip_whitespace(s);

    if (s->pos >= s->src_len) {
        s->tok_pos.line = s->line;
        s->tok_pos.column = s->col;
        s->tok_pos.offset = pos_offset(s->pos);
        s->tok_type = NEVERC_SCANNER_EOF;
        s->tok_buf[0] = '\0';
        return NEVERC_SCANNER_EOF;
    }

    s->tok_pos.line = s->line;
    s->tok_pos.column = s->col;
    s->tok_pos.offset = pos_offset(s->pos);

    size_t ch_start = s->pos;
    int ch = next_ch(s);

    if ((s->mode & NEVERC_SCAN_IDENTS) && is_ident_rune(ch, 0)) {
        emit_consumed(s, ch_start);
        s->tok_type = scan_identifier(s);
    } else if ((s->mode & (NEVERC_SCAN_INTS | NEVERC_SCAN_FLOATS)) && is_digit(ch)) {
        s->tok_type = scan_number(s, ch);
    } else if ((s->mode & NEVERC_SCAN_FLOATS) && ch == '.' &&
               s->pos < s->src_len && is_digit(peek_ch(s))) {
        s->tok_type = scan_number(s, ch);
    } else if ((s->mode & NEVERC_SCAN_CHARS) && ch == '\'') {
        s->tok_type = scan_string(s, '\'');
    } else if ((s->mode & NEVERC_SCAN_STRINGS) && ch == '"') {
        s->tok_type = scan_string(s, '"');
    } else if ((s->mode & NEVERC_SCAN_RAWSTRINGS) && ch == '`') {
        s->tok_type = scan_raw_string(s);
    } else if ((s->mode & NEVERC_SCAN_COMMENTS) && ch == '/') {
        int nxt = peek_ch(s);
        if (nxt == '/' || nxt == '*') {
            next_ch(s);
            s->tok_type = scan_comment(s, nxt);
            if (s->mode & NEVERC_SCAN_SKIP_COMMENTS) {
                if (scanner_token_overflow(s))
                    scanner_add_error(s);
                s->tok_len = 0;
                scanner_set_token_overflow(s, 0);
                goto again;
            }
        } else {
            emit_consumed(s, ch_start);
            s->tok_type = ch;
        }
    } else {
        emit_consumed(s, ch_start);
        s->tok_type = ch;
    }

    s->tok_buf[s->tok_len] = '\0';
    if (scanner_token_overflow(s))
        scanner_add_error(s);
    return s->tok_type;
}

const char *neverc_scanner_token_text(const neverc_scanner_t *s, size_t *len) {
    if (!s) {
        if (len) *len = 0;
        return "";
    }
    if (len) *len = s->tok_len;
    return s->tok_buf;
}

neverc_scanner_pos_t neverc_scanner_position(const neverc_scanner_t *s) {
    neverc_scanner_pos_t empty = {0, 0, 0};
    return s ? s->tok_pos : empty;
}

int neverc_scanner_error_count(const neverc_scanner_t *s) {
    return s ? scanner_error_count_value(s) : 0;
}

int neverc_scanner_peek(neverc_scanner_t *s) {
    if (!s || s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    size_t saved_pos = s->pos;
    int saved_line = s->line, saved_col = s->col;
    uint32_t saved_state = scanner_state_load(s);
    skip_whitespace(s);
    /* Match Scan(): skipped comments are whitespace, not the next token. */
    if ((s->mode & NEVERC_SCAN_COMMENTS) && (s->mode & NEVERC_SCAN_SKIP_COMMENTS)) {
        while (at_comment(s)) {
            skip_one_comment(s);
            skip_whitespace(s);
        }
    }
    int ch = peek_ch(s);
    s->pos = saved_pos;
    s->line = saved_line;
    s->col = saved_col;
    scanner_state_store(s, saved_state);
    return ch;
}

const char *neverc_scanner_token_name(int tok) {
    switch (tok) {
    case NEVERC_SCANNER_EOF:       return "EOF";
    case NEVERC_SCANNER_IDENT:     return "Ident";
    case NEVERC_SCANNER_INT:       return "Int";
    case NEVERC_SCANNER_FLOAT:     return "Float";
    case NEVERC_SCANNER_CHAR:      return "Char";
    case NEVERC_SCANNER_STRING:    return "String";
    case NEVERC_SCANNER_RAWSTRING: return "RawString";
    case NEVERC_SCANNER_COMMENT:   return "Comment";
    default: {
        if (tok >= 33 && tok <= 126) {
#define R(c) [c] = { (char)c, 0 }
            static const char ascii[127][2] = {
                R('!'), R('"'), R('#'), R('$'), R('%'), R('&'), R('\''),
                R('('), R(')'), R('*'), R('+'), R(','), R('-'), R('.'),
                R('/'), R('0'), R('1'), R('2'), R('3'), R('4'), R('5'),
                R('6'), R('7'), R('8'), R('9'), R(':'), R(';'), R('<'),
                R('='), R('>'), R('?'), R('@'), R('A'), R('B'), R('C'),
                R('D'), R('E'), R('F'), R('G'), R('H'), R('I'), R('J'),
                R('K'), R('L'), R('M'), R('N'), R('O'), R('P'), R('Q'),
                R('R'), R('S'), R('T'), R('U'), R('V'), R('W'), R('X'),
                R('Y'), R('Z'), R('['), R('\\'), R(']'), R('^'), R('_'),
                R('`'), R('a'), R('b'), R('c'), R('d'), R('e'), R('f'),
                R('g'), R('h'), R('i'), R('j'), R('k'), R('l'), R('m'),
                R('n'), R('o'), R('p'), R('q'), R('r'), R('s'), R('t'),
                R('u'), R('v'), R('w'), R('x'), R('y'), R('z'), R('{'),
                R('|'), R('}'), R('~')
            };
#undef R
            return ascii[tok];
        }
        return "?";
    }
    }
}
