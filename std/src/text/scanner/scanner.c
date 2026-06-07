#include "neverc/text/scanner.h"
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

static int peek_ch(neverc_scanner_t *s) {
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    return (unsigned char)s->src[s->pos];
}

static int next_ch(neverc_scanner_t *s) {
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    int ch = (unsigned char)s->src[s->pos++];
    if (ch == '\n') { s->line++; s->col = 1; }
    else { s->col++; }
    return ch;
}

static void emit(neverc_scanner_t *s, int ch) {
    if (s->tok_len < sizeof(s->tok_buf) - 1)
        s->tok_buf[s->tok_len++] = (char)ch;
}

static int is_whitespace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static void skip_whitespace(neverc_scanner_t *s) {
    while (s->pos < s->src_len && is_whitespace(peek_ch(s)))
        next_ch(s);
}

static int scan_identifier(neverc_scanner_t *s) {
    while (s->pos < s->src_len) {
        int ch = peek_ch(s);
        if (!is_letter(ch) && !is_digit(ch)) break;
        emit(s, ch);
        next_ch(s);
    }
    return NEVERC_SCANNER_IDENT;
}

static int scan_number(neverc_scanner_t *s, int first) {
    int is_float = (first == '.');
    emit(s, first);

    if (first == '.' ) {
        while (s->pos < s->src_len && is_digit(peek_ch(s)))
            emit(s, next_ch(s));
        if (s->pos < s->src_len && (peek_ch(s) == 'e' || peek_ch(s) == 'E')) {
            emit(s, next_ch(s));
            if (s->pos < s->src_len && (peek_ch(s) == '+' || peek_ch(s) == '-'))
                emit(s, next_ch(s));
            while (s->pos < s->src_len && is_digit(peek_ch(s)))
                emit(s, next_ch(s));
        }
        return NEVERC_SCANNER_FLOAT;
    }

    if (first == '0' && s->pos < s->src_len) {
        int ch = peek_ch(s);
        if (ch == 'x' || ch == 'X') {
            emit(s, next_ch(s));
            while (s->pos < s->src_len && is_hex_digit(peek_ch(s)))
                emit(s, next_ch(s));
            if (s->pos < s->src_len && peek_ch(s) == '.') {
                is_float = 1;
                emit(s, next_ch(s));
                while (s->pos < s->src_len && is_hex_digit(peek_ch(s)))
                    emit(s, next_ch(s));
            }
            if (s->pos < s->src_len && (peek_ch(s) == 'p' || peek_ch(s) == 'P')) {
                is_float = 1;
                emit(s, next_ch(s));
                if (s->pos < s->src_len && (peek_ch(s) == '+' || peek_ch(s) == '-'))
                    emit(s, next_ch(s));
                while (s->pos < s->src_len && is_digit(peek_ch(s)))
                    emit(s, next_ch(s));
            }
            return is_float ? NEVERC_SCANNER_FLOAT : NEVERC_SCANNER_INT;
        }
        if (ch == 'b' || ch == 'B') {
            emit(s, next_ch(s));
            while (s->pos < s->src_len && (peek_ch(s) == '0' || peek_ch(s) == '1'))
                emit(s, next_ch(s));
            return NEVERC_SCANNER_INT;
        }
        if (ch == 'o' || ch == 'O') {
            emit(s, next_ch(s));
            while (s->pos < s->src_len && is_oct_digit(peek_ch(s)))
                emit(s, next_ch(s));
            return NEVERC_SCANNER_INT;
        }
        while (s->pos < s->src_len && is_oct_digit(peek_ch(s)))
            emit(s, next_ch(s));
    }

    while (s->pos < s->src_len && is_digit(peek_ch(s)))
        emit(s, next_ch(s));

    if (s->pos < s->src_len && peek_ch(s) == '.') {
        int next_pos = (int)(s->pos + 1);
        if ((size_t)next_pos < s->src_len && is_digit(s->src[next_pos])) {
            is_float = 1;
            emit(s, next_ch(s));
            while (s->pos < s->src_len && is_digit(peek_ch(s)))
                emit(s, next_ch(s));
        } else if ((size_t)next_pos >= s->src_len) {
            is_float = 1;
            emit(s, next_ch(s));
        }
    }

    if (s->pos < s->src_len && (peek_ch(s) == 'e' || peek_ch(s) == 'E')) {
        is_float = 1;
        emit(s, next_ch(s));
        if (s->pos < s->src_len && (peek_ch(s) == '+' || peek_ch(s) == '-'))
            emit(s, next_ch(s));
        while (s->pos < s->src_len && is_digit(peek_ch(s)))
            emit(s, next_ch(s));
    }

    if (s->pos < s->src_len && (peek_ch(s) == 'i'))
        emit(s, next_ch(s));

    if (is_float && (s->mode & NEVERC_SCAN_FLOATS))
        return NEVERC_SCANNER_FLOAT;
    if (is_float && (s->mode & NEVERC_SCAN_INTS))
        return NEVERC_SCANNER_FLOAT;
    return is_float ? NEVERC_SCANNER_FLOAT : NEVERC_SCANNER_INT;
}

static void scan_escape(neverc_scanner_t *s, int quote) {
    int ch = next_ch(s);
    if (ch == NEVERC_SCANNER_EOF) return;
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
}

static int scan_string(neverc_scanner_t *s, int quote) {
    emit(s, quote);
    while (s->pos < s->src_len) {
        int ch = next_ch(s);
        if (ch == quote) { emit(s, ch); break; }
        if (ch == '\\') { emit(s, ch); scan_escape(s, quote); continue; }
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
        while (s->pos < s->src_len && peek_ch(s) != '\n')
            emit(s, next_ch(s));
    } else {
        int depth = 1;
        while (s->pos < s->src_len && depth > 0) {
            int ch = next_ch(s);
            emit(s, ch);
            if (ch == '*' && s->pos < s->src_len && peek_ch(s) == '/') {
                emit(s, next_ch(s));
                depth--;
            } else if (ch == '/' && s->pos < s->src_len && peek_ch(s) == '*') {
                emit(s, next_ch(s));
                depth++;
            }
        }
    }
    return NEVERC_SCANNER_COMMENT;
}

void neverc_scanner_init(neverc_scanner_t *s, const char *src, size_t len) {
    memset(s, 0, sizeof(*s));
    s->src = src;
    s->src_len = len;
    s->mode = NEVERC_SCAN_GO_TOKENS;
    s->line = 1;
    s->col = 1;
}

void neverc_scanner_set_mode(neverc_scanner_t *s, unsigned mode) {
    s->mode = mode;
}

int neverc_scanner_scan(neverc_scanner_t *s) {
    s->tok_len = 0;

again:
    skip_whitespace(s);

    if (s->pos >= s->src_len) {
        s->tok_type = NEVERC_SCANNER_EOF;
        s->tok_buf[0] = '\0';
        return NEVERC_SCANNER_EOF;
    }

    s->tok_pos.line = s->line;
    s->tok_pos.column = s->col;
    s->tok_pos.offset = (int)s->pos;

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
                s->tok_len = 0;
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
    return s->tok_type;
}

const char *neverc_scanner_token_text(const neverc_scanner_t *s, size_t *len) {
    if (len) *len = s->tok_len;
    return s->tok_buf;
}

neverc_scanner_pos_t neverc_scanner_position(const neverc_scanner_t *s) {
    return s->tok_pos;
}

int neverc_scanner_peek(neverc_scanner_t *s) {
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    size_t saved_pos = s->pos;
    int saved_line = s->line, saved_col = s->col;
    skip_whitespace(s);
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
    default: return "?";
    }
}
