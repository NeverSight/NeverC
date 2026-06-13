/*
 * A/B benchmark + correctness check: strconv Quote.
 *
 *  - old_quote — the previous library quoter, reproduced verbatim: it runs
 *      every byte through neverc_utf8_decode_rune and append_escaped_rune
 *      (with a per-character buf_putc and grow check), even for plain
 *      printable ASCII that quotes to itself.
 *
 *  - neverc_strconv_quote (library) — the new quoter: same slow path, but a
 *      fast path first bulk-copies runs of self-representing printable ASCII
 *      (0x20..0x7E except '"' and '\') with a single buf_puts.
 *
 * The fast path is behavior-preserving, so every case asserts the new output
 * is byte-for-byte identical to the old output before timing.
 *
 * Build (macOS dead-strip drops the unused strconv<->math deps of quote.c):
 *   cc -O2 -std=c11 -I std/include -Wl,-dead_strip -o /tmp/quote_bench \
 *      tests/neverc/std/quote_bench.c std/src/strconv/quote.c \
 *      std/src/unicode/utf8/utf8.c std/src/unicode/unicode.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/strconv.h"
#include "neverc/std/unicode/utf8.h"
#include "neverc/std/unicode.h"

/* ============================================================
 * OLD quoter — verbatim reproduction of the previous library
 * ============================================================ */
static const char o_lowerhex[] = "0123456789abcdef";

static int o_is_print_rune(uint32_t r) {
    if (r <= 0x7F) return (r >= 0x20 && r <= 0x7E);
    return neverc_unicode_is_print(r);
}
static int o_is_graphic_rune(uint32_t r) {
    if (o_is_print_rune(r)) return 1;
    return neverc_unicode_is_graphic(r);
}

typedef struct { char *data; size_t len, cap; } o_strbuf;
static void o_buf_init(o_strbuf *b, size_t initial) {
    b->cap = initial < 64 ? 64 : initial;
    b->data = (char *)malloc(b->cap);
    b->len = 0;
}
static void o_buf_grow(o_strbuf *b, size_t extra) {
    if (b->len + extra + 1 > b->cap) {
        size_t nc = b->cap * 2;
        if (nc < b->len + extra + 1) nc = b->len + extra + 1;
        b->data = (char *)realloc(b->data, nc);
        b->cap = nc;
    }
}
static void o_buf_putc(o_strbuf *b, char c) { o_buf_grow(b, 1); b->data[b->len++] = c; }
static void o_buf_puts(o_strbuf *b, const char *s, size_t n) {
    o_buf_grow(b, n); memcpy(b->data + b->len, s, n); b->len += n;
}
static char *o_buf_finish(o_strbuf *b) { o_buf_grow(b, 1); b->data[b->len] = '\0'; return b->data; }

static void o_append_escaped_rune(o_strbuf *b, uint32_t r, char quote,
                                  int ascii_only, int graphic_only) {
    if (r == (uint32_t)quote || r == '\\') {
        o_buf_putc(b, '\\'); o_buf_putc(b, (char)r); return;
    }
    if (ascii_only) {
        if (r < NEVERC_UTF8_RUNE_SELF && o_is_print_rune(r)) { o_buf_putc(b, (char)r); return; }
    } else if (o_is_print_rune(r) || (graphic_only && o_is_graphic_rune(r))) {
        uint8_t enc[4];
        int n = neverc_utf8_encode_rune(enc, r);
        o_buf_puts(b, (const char *)enc, (size_t)n);
        return;
    }
    switch (r) {
    case '\a': o_buf_puts(b, "\\a", 2); return;
    case '\b': o_buf_puts(b, "\\b", 2); return;
    case '\f': o_buf_puts(b, "\\f", 2); return;
    case '\n': o_buf_puts(b, "\\n", 2); return;
    case '\r': o_buf_puts(b, "\\r", 2); return;
    case '\t': o_buf_puts(b, "\\t", 2); return;
    case '\v': o_buf_puts(b, "\\v", 2); return;
    }
    if (r < ' ' || r == 0x7F) {
        o_buf_puts(b, "\\x", 2);
        o_buf_putc(b, o_lowerhex[(r >> 4) & 0xF]);
        o_buf_putc(b, o_lowerhex[r & 0xF]);
    } else if (!neverc_utf8_valid_rune(r)) {
        o_buf_puts(b, "\\ufffd", 6);
    } else if (r < 0x10000) {
        o_buf_puts(b, "\\u", 2);
        o_buf_putc(b, o_lowerhex[(r >> 12) & 0xF]);
        o_buf_putc(b, o_lowerhex[(r >> 8) & 0xF]);
        o_buf_putc(b, o_lowerhex[(r >> 4) & 0xF]);
        o_buf_putc(b, o_lowerhex[r & 0xF]);
    } else {
        o_buf_puts(b, "\\U", 2);
        for (int sh = 28; sh >= 0; sh -= 4)
            o_buf_putc(b, o_lowerhex[(r >> sh) & 0xF]);
    }
}

static char *old_quote(const char *s) {
    size_t slen = strlen(s);
    o_strbuf b;
    o_buf_init(&b, slen + 2);
    o_buf_putc(&b, '"');
    size_t i = 0;
    while (i < slen) {
        uint32_t r;
        int width;
        neverc_utf8_decode_rune((const uint8_t *)s + i, slen - i, &r, &width);
        if (width == 1 && r == NEVERC_UTF8_RUNE_ERROR) {
            o_buf_puts(&b, "\\x", 2);
            o_buf_putc(&b, o_lowerhex[((uint8_t)s[i] >> 4) & 0xF]);
            o_buf_putc(&b, o_lowerhex[(uint8_t)s[i] & 0xF]);
            i++;
            continue;
        }
        o_append_escaped_rune(&b, r, '"', 0, 0);
        i += (size_t)width;
    }
    o_buf_putc(&b, '"');
    return o_buf_finish(&b);
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile size_t sink;

static char *make_repeat(const char *pattern, size_t target_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    return s;
}

static void bench_case(const char *label, const char *input) {
    size_t in_len = strlen(input);
    char *o = old_quote(input);
    char *n = neverc_strconv_quote(input);
    if (!o || !n || strcmp(o, n) != 0) {
        printf("%-18s  CORRECTNESS FAIL\n", label);
        free(o); free(n); return;
    }
    size_t out_len = strlen(n);
    free(o); free(n);

    int iters = (int)(200000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { char *q = old_quote(input); sink = (size_t)q[0]; free(q); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { char *q = neverc_strconv_quote(input); sink = (size_t)q[0]; free(q); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-18s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %zu B)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, in_len, out_len);
}

/* Extra edge-case correctness coverage beyond the timed cases. */
static void correctness_extra(void) {
    const char *cases[] = {
        "", "a", "~", " ", "\x7f", "\\", "\"",
        "\x01\x02\x03", "tab\there\nline",
        "say \"hi\" to \\everyone\\",
        "h\xc3\xa9llo w\xc3\xb6rld \xe4\xb8\x96\xe7\x95\x8c",  /* héllo wörld 世界 */
        "mix \xff\xfe bad bytes",
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int ok = 0;
    for (int i = 0; i < n; i++) {
        char *o = old_quote(cases[i]);
        char *nw = neverc_strconv_quote(cases[i]);
        if (o && nw && strcmp(o, nw) == 0) ok++;
        else printf("  EDGE FAIL [%d]: old=\"%s\" new=\"%s\"\n", i, o ? o : "(null)", nw ? nw : "(null)");
        free(o); free(nw);
    }
    printf("edge cases: %d/%d identical\n", ok, n);
}

int main(void) {
    printf("=== strconv Quote: ASCII fast-path bulk-copy (new) vs per-byte rune decode (old) ===\n");
    printf("%-18s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    char *plain = make_repeat("The quick brown fox jumps over the lazy dog. ", 4096);
    bench_case("ascii_plain", plain);

    char *escapes = make_repeat("name=value; line\ttab; path/to/file; n=42, ", 4096);
    bench_case("ascii_light_esc", escapes);

    char *quoted = make_repeat("key=\"v\"\n", 4096);
    bench_case("ascii_heavy_esc", quoted);

    char *utf8 = make_repeat("caf\xc3\xa9 \xe4\xb8\x96\xe7\x95\x8c ", 4096);
    bench_case("utf8_mixed", utf8);

    free(plain); free(escapes); free(quoted); free(utf8);

    printf("\n");
    correctness_extra();
    printf("\n=== Done ===\n");
    return 0;
}
