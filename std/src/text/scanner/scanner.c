#include "neverc/std/text/scanner.h"
#include <limits.h>
#include <string.h>

static int is_letter(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static int is_digit(int ch) {
    return ch >= '0' && ch <= '9';
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

static int peek_ch(neverc_scanner_t *s) {
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    return (unsigned char)s->src[s->pos];
}

static int next_ch(neverc_scanner_t *s) {
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    int ch = (unsigned char)s->src[s->pos++];
    if (ch == '\n') {
        if (s->line < INT_MAX) s->line++;
        s->col = 1;
    } else {
        add_col(s, 1);
    }
    return ch;
}

static void emit(neverc_scanner_t *s, int ch) {
    if (s->tok_len < sizeof(s->tok_buf) - 1)
        s->tok_buf[s->tok_len++] = (char)ch;
    else
        s->tok_overflow = 1;
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
    size_t start = s->pos;
    size_t i = start;
    while (i < len && nci_ident_char[(unsigned char)src[i]])
        i++;

    size_t run = i - start;
    /* Bulk-copy the run, preserving the per-byte emit() cap (sizeof-1). */
    size_t cap = sizeof(s->tok_buf) - 1;
    size_t ncopy = 0;
    if (s->tok_len < cap) {
        size_t space = cap - s->tok_len;
        ncopy = run < space ? run : space;
        memcpy(s->tok_buf + s->tok_len, src + start, ncopy);
        s->tok_len += ncopy;
    }
    if (ncopy < run)
        s->tok_overflow = 1;
    /* Identifier bytes never include '\n', so only the column advances. */
    s->pos = i;
    add_col(s, run);
    return NEVERC_SCANNER_IDENT;
}

/* Go text/scanner digits(): '_' and, for base<=10, every decimal digit stay
 * in the token (invalid digits for 0b/0o are still consumed). */
static void scan_digits(neverc_scanner_t *s, int base) {
    for (;;) {
        int ch = peek_ch(s);
        if (ch == NEVERC_SCANNER_EOF) return;
        if (ch == '_') {
            emit(s, next_ch(s));
            continue;
        }
        if (base > 10) {
            if (!is_hex_digit(ch)) return;
        } else if (!is_digit(ch)) {
            return;
        }
        emit(s, next_ch(s));
    }
}

static int scan_number(neverc_scanner_t *s, int first) {
    int is_float = (first == '.');
    int base = 10;
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
        scan_digits(s, base);

    /* Go: '.' after the mantissa starts a float whenever ScanFloats is set,
     * including 0b/0o prefixes and with no fractional digits ("1.", "0b1.0"). */
    if ((s->mode & NEVERC_SCAN_FLOATS) && s->pos < s->src_len &&
        peek_ch(s) == '.') {
        is_float = 1;
        emit(s, next_ch(s));
    }

    if (is_float)
        scan_digits(s, base);

    /* Go accepts e/E and p/P exponents under ScanFloats for every prefix
     * (invalid combinations are still one Float token). */
    if ((s->mode & NEVERC_SCAN_FLOATS) && s->pos < s->src_len) {
        int ch = peek_ch(s);
        if (ch == 'e' || ch == 'E' || ch == 'p' || ch == 'P') {
            is_float = 1;
            emit(s, next_ch(s));
            if (s->pos < s->src_len && (peek_ch(s) == '+' || peek_ch(s) == '-'))
                emit(s, next_ch(s));
            scan_digits(s, 10);
        }
    }

    return is_float ? NEVERC_SCANNER_FLOAT : NEVERC_SCANNER_INT;
}

/* Returns the byte after '\\'. A source newline is not part of the literal
 * (Go: "literal not terminated") and must not be emitted into tok_buf. */
static int scan_escape(neverc_scanner_t *s, int quote) {
    int ch = next_ch(s);
    if (ch == NEVERC_SCANNER_EOF || ch == '\n') return ch;
    emit(s, ch);
    if (ch == 'x') {
        for (int i = 0; i < 2 && s->pos < s->src_len && is_hex_digit(peek_ch(s)); i++)
            emit(s, next_ch(s));
    } else if (ch == 'u') {
        for (int i = 0; i < 4 && s->pos < s->src_len && is_hex_digit(peek_ch(s)); i++)
            emit(s, next_ch(s));
    } else if (ch == 'U') {
        for (int i = 0; i < 8 && s->pos < s->src_len && is_hex_digit(peek_ch(s)); i++)
            emit(s, next_ch(s));
    } else if (is_oct_digit(ch)) {
        for (int i = 0; i < 2 && s->pos < s->src_len && is_oct_digit(peek_ch(s)); i++)
            emit(s, next_ch(s));
    }
    (void)quote;
    return ch;
}

static int scan_string(neverc_scanner_t *s, int quote) {
    emit(s, quote);
    while (s->pos < s->src_len) {
        int ch = next_ch(s);
        if (ch == quote) { emit(s, ch); break; }
        if (ch == '\\') {
            emit(s, ch);
            if (scan_escape(s, quote) == '\n') break;
            continue;
        }
        if (ch == '\n') break;
        emit(s, ch);
    }
    return (quote == '\'') ? NEVERC_SCANNER_CHAR : NEVERC_SCANNER_STRING;
}

static int scan_raw_string(neverc_scanner_t *s) {
    emit(s, '`');
    while (s->pos < s->src_len) {
        int ch = next_ch(s);
        if (ch == '`') { emit(s, ch); break; }
        emit(s, ch);
    }
    return NEVERC_SCANNER_RAWSTRING;
}

static int scan_comment(neverc_scanner_t *s, int second) {
    emit(s, '/');
    emit(s, second);
    if (second == '/') {
        /* Line comment: bulk-copy up to the next '\n' (none of which it can
         * contain) instead of emitting one byte at a time. */
        const char *src = s->src;
        size_t cur = s->pos;
        size_t len = s->src_len;
        const char *nl = (const char *)memchr(src + cur, '\n', len - cur);
        size_t end = nl ? (size_t)(nl - src) : len;
        size_t run = end - cur;
        size_t cap = sizeof(s->tok_buf) - 1;
        size_t ncopy = 0;
        if (s->tok_len < cap) {
            size_t space = cap - s->tok_len;
            ncopy = run < space ? run : space;
            memcpy(s->tok_buf + s->tok_len, src + cur, ncopy);
            s->tok_len += ncopy;
        }
        if (ncopy < run)
            s->tok_overflow = 1;
        s->pos = end;
        add_col(s, run);
    } else {
        /* C/Go block comments are not nested: the first * / ends the comment. */
        while (s->pos < s->src_len) {
            int ch = next_ch(s);
            emit(s, ch);
            if (ch == '*' && s->pos < s->src_len && peek_ch(s) == '/') {
                emit(s, next_ch(s));
                break;
            }
        }
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
    s->tok_overflow = 0;

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

    int ch = next_ch(s);

    if ((s->mode & NEVERC_SCAN_IDENTS) && is_letter(ch)) {
        emit(s, ch);
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
                if (s->tok_overflow && s->errors < INT_MAX)
                    s->errors++;
                s->tok_len = 0;
                s->tok_overflow = 0;
                goto again;
            }
        } else {
            emit(s, ch);
            s->tok_type = ch;
        }
    } else {
        emit(s, ch);
        s->tok_type = ch;
    }

    s->tok_buf[s->tok_len] = '\0';
    if (s->tok_overflow && s->errors < INT_MAX)
        s->errors++;
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
    return s ? s->errors : 0;
}

int neverc_scanner_peek(neverc_scanner_t *s) {
    if (!s || s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    size_t saved_pos = s->pos;
    int saved_line = s->line, saved_col = s->col;
    skip_whitespace(s);
    /* Match Scan(): skipped comments are whitespace, not the next token. */
    if ((s->mode & NEVERC_SCAN_COMMENTS) && (s->mode & NEVERC_SCAN_SKIP_COMMENTS)) {
        while (at_comment(s)) {
            skip_one_comment(s);
            skip_whitespace(s);
        }
    }
    int ch = (s->pos < s->src_len) ? (unsigned char)s->src[s->pos] : NEVERC_SCANNER_EOF;
    s->pos = saved_pos;
    s->line = saved_line;
    s->col = saved_col;
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
