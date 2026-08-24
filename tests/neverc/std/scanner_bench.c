/*
 * A/B benchmark + correctness check: text/scanner tokenizer.
 *
 *  - old_scanner_scan — the previous library scanner, reproduced verbatim: it
 *      walks identifiers and "//" line comments one byte at a time, calling
 *      peek_ch / next_ch / emit per byte (each a bounds check, and next_ch a
 *      newline branch + column update).
 *
 *  - neverc_scanner_scan (library) — the current UTF-8 scanner. ASCII
 *      identifier runs still use the 256-entry continuation table and one
 *      memcpy; non-ASCII input is decoded as runes so identifiers, token text,
 *      and rune-based columns match the public scanner contract.
 *
 * The token stream must be bit-for-bit identical, so every case asserts that
 * both scanners yield the same sequence of (type, text, line, column, offset)
 * tokens before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/scanner_bench \
 *      tests/neverc/std/scanner_bench.c std/src/text/scanner/scanner.c \
 *      std/src/unicode/unicode.c std/src/unicode/utf8/utf8.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/text/scanner.h"

/* ============================================================
 * OLD scanner — verbatim reproduction of the previous library
 * ============================================================ */
static int old_is_letter(int ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}
static int old_is_digit(int ch) { return ch >= '0' && ch <= '9'; }
static int old_is_hex_digit(int ch) {
    return old_is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}
static int old_is_oct_digit(int ch) { return ch >= '0' && ch <= '7'; }

static int old_peek_ch(neverc_scanner_t *s) {
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    return (unsigned char)s->src[s->pos];
}
static int old_next_ch(neverc_scanner_t *s) {
    if (s->pos >= s->src_len) return NEVERC_SCANNER_EOF;
    int ch = (unsigned char)s->src[s->pos++];
    if (ch == '\n') { s->line++; s->col = 1; }
    else { s->col++; }
    return ch;
}
static void old_emit(neverc_scanner_t *s, int ch) {
    if (s->tok_len < sizeof(s->tok_buf) - 1)
        s->tok_buf[s->tok_len++] = (char)ch;
}
static int old_is_whitespace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}
static void old_skip_whitespace(neverc_scanner_t *s) {
    while (s->pos < s->src_len && old_is_whitespace(old_peek_ch(s)))
        old_next_ch(s);
}

static int old_scan_identifier(neverc_scanner_t *s) {
    while (s->pos < s->src_len) {
        int ch = old_peek_ch(s);
        if (!old_is_letter(ch) && !old_is_digit(ch)) break;
        old_emit(s, ch);
        old_next_ch(s);
    }
    return NEVERC_SCANNER_IDENT;
}

static int old_scan_number(neverc_scanner_t *s, int first) {
    int is_float = (first == '.');
    old_emit(s, first);

    if (first == '.') {
        while (s->pos < s->src_len && old_is_digit(old_peek_ch(s)))
            old_emit(s, old_next_ch(s));
        if (s->pos < s->src_len && (old_peek_ch(s) == 'e' || old_peek_ch(s) == 'E')) {
            old_emit(s, old_next_ch(s));
            if (s->pos < s->src_len && (old_peek_ch(s) == '+' || old_peek_ch(s) == '-'))
                old_emit(s, old_next_ch(s));
            while (s->pos < s->src_len && old_is_digit(old_peek_ch(s)))
                old_emit(s, old_next_ch(s));
        }
        return NEVERC_SCANNER_FLOAT;
    }

    if (first == '0' && s->pos < s->src_len) {
        int ch = old_peek_ch(s);
        if (ch == 'x' || ch == 'X') {
            old_emit(s, old_next_ch(s));
            while (s->pos < s->src_len && old_is_hex_digit(old_peek_ch(s)))
                old_emit(s, old_next_ch(s));
            if (s->pos < s->src_len && old_peek_ch(s) == '.') {
                is_float = 1;
                old_emit(s, old_next_ch(s));
                while (s->pos < s->src_len && old_is_hex_digit(old_peek_ch(s)))
                    old_emit(s, old_next_ch(s));
            }
            if (s->pos < s->src_len && (old_peek_ch(s) == 'p' || old_peek_ch(s) == 'P')) {
                is_float = 1;
                old_emit(s, old_next_ch(s));
                if (s->pos < s->src_len && (old_peek_ch(s) == '+' || old_peek_ch(s) == '-'))
                    old_emit(s, old_next_ch(s));
                while (s->pos < s->src_len && old_is_digit(old_peek_ch(s)))
                    old_emit(s, old_next_ch(s));
            }
            return is_float ? NEVERC_SCANNER_FLOAT : NEVERC_SCANNER_INT;
        }
        if (ch == 'b' || ch == 'B') {
            old_emit(s, old_next_ch(s));
            while (s->pos < s->src_len && (old_peek_ch(s) == '0' || old_peek_ch(s) == '1'))
                old_emit(s, old_next_ch(s));
            return NEVERC_SCANNER_INT;
        }
        if (ch == 'o' || ch == 'O') {
            old_emit(s, old_next_ch(s));
            while (s->pos < s->src_len && old_is_oct_digit(old_peek_ch(s)))
                old_emit(s, old_next_ch(s));
            return NEVERC_SCANNER_INT;
        }
        while (s->pos < s->src_len && old_is_oct_digit(old_peek_ch(s)))
            old_emit(s, old_next_ch(s));
    }

    while (s->pos < s->src_len && old_is_digit(old_peek_ch(s)))
        old_emit(s, old_next_ch(s));

    if (s->pos < s->src_len && old_peek_ch(s) == '.') {
        int next_pos = (int)(s->pos + 1);
        if ((size_t)next_pos < s->src_len && old_is_digit(s->src[next_pos])) {
            is_float = 1;
            old_emit(s, old_next_ch(s));
            while (s->pos < s->src_len && old_is_digit(old_peek_ch(s)))
                old_emit(s, old_next_ch(s));
        } else if ((size_t)next_pos >= s->src_len) {
            is_float = 1;
            old_emit(s, old_next_ch(s));
        }
    }

    if (s->pos < s->src_len && (old_peek_ch(s) == 'e' || old_peek_ch(s) == 'E')) {
        is_float = 1;
        old_emit(s, old_next_ch(s));
        if (s->pos < s->src_len && (old_peek_ch(s) == '+' || old_peek_ch(s) == '-'))
            old_emit(s, old_next_ch(s));
        while (s->pos < s->src_len && old_is_digit(old_peek_ch(s)))
            old_emit(s, old_next_ch(s));
    }

    if (s->pos < s->src_len && (old_peek_ch(s) == 'i'))
        old_emit(s, old_next_ch(s));

    if (is_float && (s->mode & NEVERC_SCAN_FLOATS))
        return NEVERC_SCANNER_FLOAT;
    if (is_float && (s->mode & NEVERC_SCAN_INTS))
        return NEVERC_SCANNER_FLOAT;
    return is_float ? NEVERC_SCANNER_FLOAT : NEVERC_SCANNER_INT;
}

static void old_scan_escape(neverc_scanner_t *s, int quote) {
    int ch = old_next_ch(s);
    if (ch == NEVERC_SCANNER_EOF) return;
    old_emit(s, ch);
    if (ch == 'x') {
        for (int i = 0; i < 2 && s->pos < s->src_len && old_is_hex_digit(old_peek_ch(s)); i++)
            old_emit(s, old_next_ch(s));
    } else if (ch == 'u') {
        for (int i = 0; i < 4 && s->pos < s->src_len && old_is_hex_digit(old_peek_ch(s)); i++)
            old_emit(s, old_next_ch(s));
    } else if (ch == 'U') {
        for (int i = 0; i < 8 && s->pos < s->src_len && old_is_hex_digit(old_peek_ch(s)); i++)
            old_emit(s, old_next_ch(s));
    } else if (old_is_oct_digit(ch)) {
        for (int i = 0; i < 2 && s->pos < s->src_len && old_is_oct_digit(old_peek_ch(s)); i++)
            old_emit(s, old_next_ch(s));
    }
    (void)quote;
}

static int old_scan_string(neverc_scanner_t *s, int quote) {
    old_emit(s, quote);
    while (s->pos < s->src_len) {
        int ch = old_next_ch(s);
        if (ch == quote) { old_emit(s, ch); break; }
        if (ch == '\\') { old_emit(s, ch); old_scan_escape(s, quote); continue; }
        if (ch == '\n') break;
        old_emit(s, ch);
    }
    return (quote == '\'') ? NEVERC_SCANNER_CHAR : NEVERC_SCANNER_STRING;
}

static int old_scan_raw_string(neverc_scanner_t *s) {
    old_emit(s, '`');
    while (s->pos < s->src_len) {
        int ch = old_next_ch(s);
        if (ch == '`') { old_emit(s, ch); break; }
        old_emit(s, ch);
    }
    return NEVERC_SCANNER_RAWSTRING;
}

static int old_scan_comment(neverc_scanner_t *s, int second) {
    old_emit(s, '/');
    old_emit(s, second);
    if (second == '/') {
        while (s->pos < s->src_len && old_peek_ch(s) != '\n')
            old_emit(s, old_next_ch(s));
    } else {
        int depth = 1;
        while (s->pos < s->src_len && depth > 0) {
            int ch = old_next_ch(s);
            old_emit(s, ch);
            if (ch == '*' && s->pos < s->src_len && old_peek_ch(s) == '/') {
                old_emit(s, old_next_ch(s));
                depth--;
            } else if (ch == '/' && s->pos < s->src_len && old_peek_ch(s) == '*') {
                old_emit(s, old_next_ch(s));
                depth++;
            }
        }
    }
    return NEVERC_SCANNER_COMMENT;
}

static int old_scanner_scan(neverc_scanner_t *s) {
    s->tok_len = 0;

again:
    old_skip_whitespace(s);

    if (s->pos >= s->src_len) {
        s->tok_type = NEVERC_SCANNER_EOF;
        s->tok_buf[0] = '\0';
        return NEVERC_SCANNER_EOF;
    }

    s->tok_pos.line = s->line;
    s->tok_pos.column = s->col;
    s->tok_pos.offset = (int)s->pos;

    int ch = old_next_ch(s);

    if ((s->mode & NEVERC_SCAN_IDENTS) && old_is_letter(ch)) {
        old_emit(s, ch);
        s->tok_type = old_scan_identifier(s);
    } else if ((s->mode & (NEVERC_SCAN_INTS | NEVERC_SCAN_FLOATS)) && old_is_digit(ch)) {
        s->tok_type = old_scan_number(s, ch);
    } else if ((s->mode & NEVERC_SCAN_FLOATS) && ch == '.' &&
               s->pos < s->src_len && old_is_digit(old_peek_ch(s))) {
        s->tok_type = old_scan_number(s, ch);
    } else if ((s->mode & NEVERC_SCAN_CHARS) && ch == '\'') {
        s->tok_type = old_scan_string(s, '\'');
    } else if ((s->mode & NEVERC_SCAN_STRINGS) && ch == '"') {
        s->tok_type = old_scan_string(s, '"');
    } else if ((s->mode & NEVERC_SCAN_RAWSTRINGS) && ch == '`') {
        s->tok_type = old_scan_raw_string(s);
    } else if ((s->mode & NEVERC_SCAN_COMMENTS) && ch == '/') {
        int nxt = old_peek_ch(s);
        if (nxt == '/' || nxt == '*') {
            old_next_ch(s);
            s->tok_type = old_scan_comment(s, nxt);
            if (s->mode & NEVERC_SCAN_SKIP_COMMENTS) {
                s->tok_len = 0;
                goto again;
            }
        } else {
            old_emit(s, ch);
            s->tok_type = ch;
        }
    } else {
        old_emit(s, ch);
        s->tok_type = ch;
    }

    s->tok_buf[s->tok_len] = '\0';
    return s->tok_type;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile unsigned long sink;

#define BENCH_MODE (NEVERC_SCAN_IDENTS | NEVERC_SCAN_INTS | NEVERC_SCAN_FLOATS | \
                    NEVERC_SCAN_CHARS | NEVERC_SCAN_STRINGS |                    \
                    NEVERC_SCAN_RAWSTRINGS | NEVERC_SCAN_COMMENTS)

static char *make_repeat(const char *pattern, size_t target_len, size_t *out_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    if (out_len) *out_len = i;
    return s;
}

typedef int (*scanfn)(neverc_scanner_t *);

/* Compare the full token streams; returns 1 if identical, else 0 and prints
 * the first divergent token. */
static int streams_identical(const char *src, size_t len, unsigned mode,
                             const char *label) {
    neverc_scanner_t a, b;
    neverc_scanner_init(&a, src, len); neverc_scanner_set_mode(&a, mode);
    neverc_scanner_init(&b, src, len); neverc_scanner_set_mode(&b, mode);
    long idx = 0;
    for (;;) {
        int ta = old_scanner_scan(&a);
        int tb = neverc_scanner_scan(&b);
        int ok = (ta == tb) &&
                 (a.tok_len == b.tok_len) &&
                 (memcmp(a.tok_buf, b.tok_buf, a.tok_len) == 0) &&
                 (a.tok_pos.line == b.tok_pos.line) &&
                 (a.tok_pos.column == b.tok_pos.column) &&
                 (a.tok_pos.offset == b.tok_pos.offset);
        if (!ok) {
            printf("  MISMATCH [%s] token #%ld:\n", label, idx);
            printf("    old: type=%d len=%zu line=%d col=%d off=%d \"%.*s\"\n",
                   ta, a.tok_len, a.tok_pos.line, a.tok_pos.column, a.tok_pos.offset,
                   (int)a.tok_len, a.tok_buf);
            printf("    new: type=%d len=%zu line=%d col=%d off=%d \"%.*s\"\n",
                   tb, b.tok_len, b.tok_pos.line, b.tok_pos.column, b.tok_pos.offset,
                   (int)b.tok_len, b.tok_buf);
            return 0;
        }
        if (ta == NEVERC_SCANNER_EOF) break;
        idx++;
    }
    return 1;
}

static double time_scan(scanfn fn, const char *src, size_t len, int iters) {
    double best = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int it = 0; it < iters; it++) {
            neverc_scanner_t s;
            neverc_scanner_init(&s, src, len);
            neverc_scanner_set_mode(&s, BENCH_MODE);
            int t;
            do { t = fn(&s); sink += s.tok_len; } while (t != NEVERC_SCANNER_EOF);
        }
        double e = now_sec() - t0; if (e < best) best = e;
    }
    return best;
}

static void bench_case(const char *label, const char *src, size_t len) {
    if (!streams_identical(src, len, BENCH_MODE, label)) {
        printf("%-18s  CORRECTNESS FAIL\n", label);
        return;
    }
    int iters = (int)(120000000 / (len + 1)); if (iters < 50) iters = 50;
    double t_old = time_scan(old_scanner_scan, src, len, iters);
    double t_new = time_scan(neverc_scanner_scan, src, len, iters);
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (%zu B/pass)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, len);
}

/* ============================================================
 * Edge-case correctness coverage
 * ============================================================ */
static int eq_case(const char *src, unsigned mode, const char *desc) {
    int ok = streams_identical(src, strlen(src), mode, desc);
    if (!ok) printf("  EDGE FAIL: %s\n", desc);
    return ok;
}

static void correctness_extra(void) {
    int ok = 0, n = 0;
    const unsigned go = NEVERC_SCAN_GO_TOKENS;     /* skips comments */
    const unsigned em = BENCH_MODE;                /* emits comments */

    n++; ok += eq_case("", em, "empty");
    n++; ok += eq_case("x", em, "single ident");
    n++; ok += eq_case("hello_world42", em, "ident with digits/underscore");
    n++; ok += eq_case("  \t leading\nws", em, "leading whitespace");
    n++; ok += eq_case("a b c d e f g", em, "many short idents");
    n++; ok += eq_case("foo+bar*baz", em, "idents with operators");
    n++; ok += eq_case("// a line comment", em, "line comment no newline");
    n++; ok += eq_case("// c1\n// c2\nident", em, "line comments + ident");
    n++; ok += eq_case("/* block */ x", em, "block comment");
    n++; ok += eq_case("/* a /* nested */ b */ y", em, "nested block comment");
    n++; ok += eq_case("a // trailing\nb", go, "GO mode skip comment");
    n++; ok += eq_case("123 0x1f 3.14 0b1010 0o17 1e9 .5", em, "numbers");
    n++; ok += eq_case("\"a string\" 'c' `raw\nstr`", em, "strings");
    n++; ok += eq_case("\"esc \\n \\x41 \\u0041\"", em, "string escapes");
    n++; ok += eq_case("ident//comment\nident2", em, "ident then comment then ident");
    n++; ok += eq_case("line1\n  line2\n\tline3", em, "multi-line positions");
    n++; ok += eq_case("a\n\n\nb", em, "blank lines positions");

    /* Truncation parity: identifier longer than tok_buf (4096). */
    {
        char *big = make_repeat("a", 5000, NULL);
        n++; ok += eq_case(big, em, "ident > tok_buf (truncation)");
        free(big);
    }
    /* Truncation parity: line comment longer than tok_buf. */
    {
        char *big = (char *)malloc(5004);
        big[0] = '/'; big[1] = '/';
        memset(big + 2, 'z', 5000);
        big[5002] = '\n'; big[5003] = '\0';
        n++; ok += eq_case(big, em, "// comment > tok_buf (truncation)");
        free(big);
    }

    printf("edge cases: %d/%d identical\n", ok, n);
}

int main(void) {
    printf("=== text/scanner: UTF-8 current vs ASCII per-byte baseline ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    size_t l1, l2, l3, l4, l5;

    char *idents = make_repeat("alpha bravo charlie delta echo foxtrot golf hotel ",
                               16384, &l1);
    bench_case("ident_heavy", idents, l1);

    char *comments = make_repeat("// the quick brown fox jumps over the lazy dog\n",
                                 16384, &l2);
    bench_case("linecomment_heavy", comments, l2);

    char *src = make_repeat(
        "func process(items []int) int {\n"
        "    total := 0 // accumulate\n"
        "    for i, v := range items {\n"
        "        total = total + v * factor\n"
        "    }\n"
        "    return total\n"
        "}\n", 16384, &l3);
    bench_case("go_source_mixed", src, l3);

    char *strings = make_repeat("\"some quoted text here\" ", 16384, &l4);
    bench_case("string_heavy", strings, l4);

    char *numbers = make_repeat("12345 0xABCDEF 3.14159 0b1011 ", 16384, &l5);
    bench_case("number_heavy", numbers, l5);

    free(idents); free(comments); free(src); free(strings); free(numbers);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
