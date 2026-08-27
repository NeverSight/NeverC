/*
 * A/B benchmark + correctness check: encoding/xml escape and char-data scan.
 *
 *  - escape: old per-byte switch with grow/realloc vs the new branchless-count +
 *    single-read copy (constant-size entity memcpy).
 *  - tokenizer char-data: the old scan walked byte-by-byte to the next '<'; the
 *    new one uses memchr. The rest of the tokenizer is identical, so the old
 *    decoder is reproduced verbatim here (with the byte loop) and both are run
 *    over a text-heavy document, checksumming every token to prove parity.
 *
 * Build:
 *   cc -O2 -std=c11 -I std/include -o /tmp/xml_bench \
 *      tests/neverc/std/xml_bench.c std/src/encoding/xml/xml.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/encoding/xml.h"

/* ============================================================
 * OLD escape — verbatim reproduction of the previous library
 * ============================================================ */
static char *old_escape(const char *s, size_t *outlen) {
    size_t slen = strlen(s);
    size_t cap = slen * 2;
    char *r = (char *)malloc(cap + 1);
    size_t wi = 0;
    for (size_t i = 0; i < slen; i++) {
        const char *esc = NULL; size_t elen = 0;
        switch (s[i]) {
            case '&': esc = "&amp;"; elen = 5; break;
            case '<': esc = "&lt;";  elen = 4; break;
            case '>': esc = "&gt;";  elen = 4; break;
            case '"': esc = "&quot;"; elen = 6; break;
            case '\'': esc = "&apos;"; elen = 6; break;
            default: break;
        }
        if (esc) {
            if (wi + elen >= cap) { cap = (wi + elen) * 2; r = (char *)realloc(r, cap + 1); }
            memcpy(r + wi, esc, elen); wi += elen;
        } else {
            if (wi + 1 >= cap) { cap *= 2; r = (char *)realloc(r, cap + 1); }
            r[wi++] = s[i];
        }
    }
    r[wi] = '\0';
    *outlen = wi;
    return r;
}

/* ============================================================
 * OLD tokenizer — verbatim, with the byte-by-byte char-data scan
 * ============================================================ */
static char *o_dup_range(const char *s, size_t n) {
    char *r = (char *)malloc(n + 1); memcpy(r, s, n); r[n] = '\0'; return r;
}
static void o_skip_ws(neverc_xml_decoder_t *d) {
    while (d->pos < d->len) {
        char c = d->src[d->pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') d->pos++; else break;
    }
}
static neverc_xml_attr_t *o_parse_attrs(neverc_xml_decoder_t *d, int *count) {
    int cap = 4;
    neverc_xml_attr_t *attrs = (neverc_xml_attr_t *)malloc(cap * sizeof(neverc_xml_attr_t));
    *count = 0;
    while (d->pos < d->len) {
        o_skip_ws(d);
        if (d->pos >= d->len || d->src[d->pos] == '>' ||
            d->src[d->pos] == '/' || d->src[d->pos] == '?') break;
        size_t ns = d->pos;
        while (d->pos < d->len && d->src[d->pos] != '=' &&
               d->src[d->pos] != ' ' && d->src[d->pos] != '>') d->pos++;
        char *name = o_dup_range(d->src + ns, d->pos - ns);
        char *value = NULL;
        if (d->pos < d->len && d->src[d->pos] == '=') {
            d->pos++;
            if (d->pos < d->len && (d->src[d->pos] == '"' || d->src[d->pos] == '\'')) {
                char q = d->src[d->pos++];
                size_t vs = d->pos;
                while (d->pos < d->len && d->src[d->pos] != q) d->pos++;
                value = o_dup_range(d->src + vs, d->pos - vs);
                if (d->pos < d->len) d->pos++;
            }
        }
        if (*count >= cap) { cap *= 2; attrs = (neverc_xml_attr_t *)realloc(attrs, cap * sizeof(neverc_xml_attr_t)); }
        attrs[*count].name = name;
        attrs[*count].value = value ? value : o_dup_range("", 0);
        (*count)++;
    }
    return attrs;
}
static int o_decode_token(neverc_xml_decoder_t *d, neverc_xml_token_t *tok) {
    memset(tok, 0, sizeof(*tok));
    if (d->pos >= d->len) { tok->type = NEVERC_XML_EOF; return 0; }
    if (d->src[d->pos] != '<') {
        size_t start = d->pos;
        while (d->pos < d->len && d->src[d->pos] != '<') d->pos++;   /* byte-by-byte */
        tok->type = NEVERC_XML_CHAR_DATA;
        tok->data = o_dup_range(d->src + start, d->pos - start);
        tok->data_len = d->pos - start;
        return 1;
    }
    d->pos++;
    if (d->pos >= d->len) return -1;
    if (d->src[d->pos] == '/') {
        d->pos++;
        size_t ns = d->pos;
        while (d->pos < d->len && d->src[d->pos] != '>') d->pos++;
        tok->type = NEVERC_XML_END_ELEMENT;
        tok->name = o_dup_range(d->src + ns, d->pos - ns);
        if (d->pos < d->len) d->pos++;
        return 1;
    }
    if (d->src[d->pos] == '!' && d->pos + 1 < d->len && d->src[d->pos+1] == '-') {
        d->pos += 3;
        size_t cs = d->pos;
        while (d->pos + 2 < d->len &&
               !(d->src[d->pos] == '-' && d->src[d->pos+1] == '-' && d->src[d->pos+2] == '>')) d->pos++;
        tok->type = NEVERC_XML_COMMENT;
        tok->data = o_dup_range(d->src + cs, d->pos - cs);
        tok->data_len = d->pos - cs;
        d->pos += 3;
        return 1;
    }
    if (d->src[d->pos] == '?') {
        d->pos++;
        size_t ps = d->pos;
        while (d->pos + 1 < d->len && !(d->src[d->pos] == '?' && d->src[d->pos+1] == '>')) d->pos++;
        tok->type = NEVERC_XML_PROC_INST;
        tok->data = o_dup_range(d->src + ps, d->pos - ps);
        tok->data_len = d->pos - ps;
        d->pos += 2;
        return 1;
    }
    size_t ns = d->pos;
    while (d->pos < d->len && d->src[d->pos] != ' ' &&
           d->src[d->pos] != '>' && d->src[d->pos] != '/') d->pos++;
    tok->type = NEVERC_XML_START_ELEMENT;
    tok->name = o_dup_range(d->src + ns, d->pos - ns);
    tok->attrs = o_parse_attrs(d, &tok->nattrs);
    if (d->pos < d->len && d->src[d->pos] == '/') { d->pos++; tok->type = NEVERC_XML_START_ELEMENT; }
    if (d->pos < d->len && d->src[d->pos] == '>') d->pos++;
    return 1;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile uint64_t sink;

static char *make_repeat(const char *pattern, size_t target_len, size_t *out_len) {
    size_t plen = strlen(pattern);
    char *s = (char *)malloc(target_len + plen + 1);
    size_t i = 0;
    while (i < target_len) { memcpy(s + i, pattern, plen); i += plen; }
    s[i] = '\0';
    if (out_len) *out_len = i;
    return s;
}

static uint64_t mix(uint64_t h, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 1099511628211ULL;
    return h;
}

/* Tokenize fully, return token count and a content checksum. */
static size_t tokenize_old(const char *doc, size_t len, uint64_t *csum) {
    neverc_xml_decoder_t d; neverc_xml_decoder_init(&d, doc, len);
    neverc_xml_token_t tok; size_t cnt = 0; uint64_t h = 0xcbf29ce484222325ULL;
    while (o_decode_token(&d, &tok) > 0) {
        h = (h ^ (uint64_t)tok.type) * 1099511628211ULL;
        if (tok.name) h = mix(h, tok.name, strlen(tok.name));
        if (tok.data) h = mix(h, tok.data, tok.data_len);
        cnt++;
        neverc_xml_token_free(&tok);
    }
    *csum = h; return cnt;
}
static size_t tokenize_new(const char *doc, size_t len, uint64_t *csum) {
    neverc_xml_decoder_t d; neverc_xml_decoder_init(&d, doc, len);
    neverc_xml_token_t tok; size_t cnt = 0; uint64_t h = 0xcbf29ce484222325ULL;
    while (neverc_xml_decode_token(&d, &tok) > 0) {
        h = (h ^ (uint64_t)tok.type) * 1099511628211ULL;
        if (tok.name) h = mix(h, tok.name, strlen(tok.name));
        if (tok.data) h = mix(h, tok.data, tok.data_len);
        cnt++;
        neverc_xml_token_free(&tok);
    }
    *csum = h; return cnt;
}

static void bench_escape(const char *label, const char *input) {
    size_t in_len = strlen(input), ol = 0, nl = 0;
    char *o = old_escape(input, &ol);
    char *n = neverc_xml_escape(input, &nl);
    if (!o || !n || ol != nl || memcmp(o, n, ol) != 0) {
        printf("%-16s  CORRECTNESS FAIL\n", label); free(o); free(n); return;
    }
    size_t out_len = nl; free(o); free(n);
    int iters = (int)(200000000 / (in_len + 1)); if (iters < 500) iters = 500;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t L; char *q = old_escape(input, &L); sink = (uint64_t)q[0]; free(q); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t L; char *q = neverc_xml_escape(input, &L); sink = (uint64_t)q[0]; free(q); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-16s  %8.1f ms  %8.1f ms  %6.2fx   (in %zu B -> %zu B)\n",
           label, t_old*1000, t_new*1000, t_old/t_new, in_len, out_len);
}

static void bench_tokenize(const char *label, const char *doc, size_t len) {
    uint64_t co = 0, cn = 0;
    size_t no = tokenize_old(doc, len, &co);
    size_t nn = tokenize_new(doc, len, &cn);
    if (no != nn || co != cn) {
        printf("%-16s  CORRECTNESS FAIL (old cnt=%zu csum=%llx, new cnt=%zu csum=%llx)\n",
               label, no, (unsigned long long)co, nn, (unsigned long long)cn);
        return;
    }
    int iters = (int)(60000000 / (len + 1)); if (iters < 50) iters = 50;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { uint64_t c; sink = tokenize_old(doc, len, &c); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { uint64_t c; sink = tokenize_new(doc, len, &c); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-16s  %8.1f ms  %8.1f ms  %6.2fx   (%zu B, %zu tokens)\n",
           label, t_old*1000, t_new*1000, t_old/t_new, len, nn);
}

int main(void) {
    printf("=== xml_escape: count+single-read (new) vs per-byte switch (old) ===\n");
    printf("%-16s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    char *plain = make_repeat("The quick brown fox jumps over the lazy dog. ", 4096, NULL);
    bench_escape("esc_plain", plain);
    char *light = make_repeat("Tom & Jerry <ran> fast, said \"go\" at 5. ", 4096, NULL);
    bench_escape("esc_light", light);
    char *heavy = make_repeat("<x a=\"1\">A&B</x> ", 4096, NULL);
    bench_escape("esc_heavy", heavy);
    free(plain); free(light); free(heavy);

    printf("\n=== xml tokenize: memchr char-data scan (new) vs byte-by-byte (old) ===\n");
    printf("%-16s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");
    size_t dl1, dl2;
    char *textdoc = make_repeat(
        "<p>Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do "
        "eiusmod tempor incididunt ut labore et dolore magna aliqua.</p>\n", 32768, &dl1);
    bench_tokenize("text_heavy", textdoc, dl1);
    char *tagdoc = make_repeat("<a><b><c/></b></a>", 32768, &dl2);
    bench_tokenize("tag_heavy", tagdoc, dl2);
    free(textdoc); free(tagdoc);

    printf("\n=== Done ===\n");
    return 0;
}
