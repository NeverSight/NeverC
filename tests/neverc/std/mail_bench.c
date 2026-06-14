/*
 * A/B benchmark + correctness check: net/mail RFC 5322 header parsing.
 *
 *  - old_parse_message — the previous library code, reproduced verbatim: it
 *      located each header's ':' and every value line's '\r'/'\n' terminator
 *      with byte-at-a-time loops.
 *
 *  - neverc_mail_parse_message (library) — now finds those delimiters with a
 *      bounded two-byte memchr (scan_first2), which libc vectorizes. The value
 *      bytes were already bulk-copied with memcpy; only the scans changed.
 *
 * The change is behavior-preserving, so each input is parsed with BOTH and the
 * results (header count, every key/value, body offset and length) must be
 * identical before any timing is reported.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/mail_bench \
 *      tests/neverc/std/mail_bench.c std/src/net/mail/mail.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "neverc/std/net/mail.h"

/* ============================================================
 * OLD primitive — verbatim reproduction of the previous library
 * ============================================================ */
static int old_parse_message(const char *data, size_t len, neverc_mail_message_t *out) {
    if (!data || !out) return -1;
    memset(out, 0, sizeof(*out));

    size_t i = 0;
    while (i < len && out->header_count < NEVERC_MAIL_MAX_HEADERS) {
        if (data[i] == '\r' && i+1 < len && data[i+1] == '\n') {
            out->body = data + i + 2;
            out->body_len = len - i - 2;
            return 0;
        }
        if (data[i] == '\n') {
            out->body = data + i + 1;
            out->body_len = len - i - 1;
            return 0;
        }

        size_t colon = i;
        while (colon < len && data[colon] != ':' && data[colon] != '\n') colon++;
        if (colon >= len || data[colon] != ':') break;

        neverc_mail_header_t *h = &out->headers[out->header_count];
        size_t klen = colon - i;
        if (klen >= sizeof(h->key)) klen = sizeof(h->key) - 1;
        memcpy(h->key, data + i, klen);
        h->key[klen] = '\0';

        size_t vstart = colon + 1;
        while (vstart < len && (data[vstart] == ' ' || data[vstart] == '\t')) vstart++;

        size_t vpos = 0;
        size_t pos = vstart;
        while (pos < len) {
            size_t line_end = pos;
            while (line_end < len && data[line_end] != '\r' && data[line_end] != '\n') line_end++;

            size_t line_len = line_end - pos;
            if (vpos + line_len + 1 < sizeof(h->value)) {
                if (vpos > 0 && pos != vstart) h->value[vpos++] = ' ';
                memcpy(h->value + vpos, data + pos, line_len);
                vpos += line_len;
            }

            if (line_end < len && data[line_end] == '\r') line_end++;
            if (line_end < len && data[line_end] == '\n') line_end++;

            if (line_end < len && (data[line_end] == ' ' || data[line_end] == '\t')) {
                while (line_end < len && (data[line_end] == ' ' || data[line_end] == '\t')) line_end++;
                pos = line_end;
            } else {
                pos = line_end;
                break;
            }
        }
        h->value[vpos] = '\0';
        out->header_count++;
        i = pos;
    }

    out->body = data + i;
    out->body_len = len - i;
    return 0;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile size_t sink;

static neverc_mail_message_t MO, MN;

static int msg_match(const char *data, size_t len) {
    int ro = old_parse_message(data, len, &MO);
    int rn = neverc_mail_parse_message(data, len, &MN);
    if (ro != rn) return 0;
    if (MO.header_count != MN.header_count) return 0;
    for (int i = 0; i < MO.header_count; i++) {
        if (strcmp(MO.headers[i].key,   MN.headers[i].key)   != 0) return 0;
        if (strcmp(MO.headers[i].value, MN.headers[i].value) != 0) return 0;
    }
    if (MO.body_len != MN.body_len) return 0;
    /* body is a pointer into data; compare the offset */
    if ((MO.body ? (size_t)(MO.body - data) : 0) !=
        (MN.body ? (size_t)(MN.body - data) : 0)) return 0;
    return 1;
}

static unsigned rng = 0x2545F491u;
static unsigned nextr(void) { rng ^= rng<<13; rng ^= rng>>17; rng ^= rng<<5; return rng; }

/* Build an RFC-5322-ish header block: `nhdr` headers each with a value of about
 * `valw` bytes, CRLF or LF terminated, optionally with folded continuation. */
static char *gen_msg(int nhdr, int valw, int crlf, int fold, size_t *out_len) {
    size_t cap = (size_t)nhdr * (size_t)(valw + 64) + 256;
    char *b = (char *)malloc(cap);
    size_t k = 0;
    const char *eol = crlf ? "\r\n" : "\n";
    size_t eoln = crlf ? 2 : 1;
    for (int i = 0; i < nhdr; i++) {
        k += (size_t)snprintf(b + k, cap - k, "X-Header-%d: ", i);
        int w = valw + (int)(nextr() % 8);
        for (int j = 0; j < w; j++) {
            if (fold && j > 0 && (j % 40) == 0) {           /* folded continuation */
                memcpy(b + k, eol, eoln); k += eoln;
                b[k++] = ' ';
            }
            b[k++] = (char)('a' + (nextr() % 26));
        }
        memcpy(b + k, eol, eoln); k += eoln;
    }
    memcpy(b + k, eol, eoln); k += eoln;                     /* blank line */
    k += (size_t)snprintf(b + k, cap - k, "this is the body\nsecond line\n");
    *out_len = k;
    return b;
}

/* ============================================================
 * Correctness
 * ============================================================ */
static int correctness(void) {
    static const char *lits[] = {
        "", "\n", "\r\n", "A: b\r\n\r\nbody", "A: b\nB: c\n\nbody",
        "Subject: hello world\r\n\r\n",
        "From: a@b.com\r\nTo: c@d.com\r\nSubject: hi\r\n\r\nhi there",
        "Folded: line one\r\n  line two\r\n  line three\r\n\r\nx",
        "NoColonLine\r\nA: b\r\n",                 /* malformed: stops at bad line */
        "Empty-Value:\r\n\r\n",
        "Spaces:    trimmed-left\r\n\r\n",
        "Tab:\tvalue\r\n\r\n",
        "LF-only: v1\nAnother: v2\n\nbody-lf",
        "Bare-CR: a\rb\r\n\r\n",                   /* bare CR inside value */
        "trailing-no-eol: value-without-terminator",
        "A: b\r\nC: d",                            /* last header no blank line */
    };
    int ok = 0, total = 0;
    for (size_t i = 0; i < sizeof(lits)/sizeof(lits[0]); i++) {
        total++;
        if (msg_match(lits[i], strlen(lits[i]))) ok++;
        else printf("  LIT FAIL [%zu] = [%.40s]\n", i, lits[i]);
    }

    /* randomized header blocks across shapes */
    for (int t = 0; t < 600; t++) {
        int nhdr = 1 + (int)(nextr() % 40);
        int valw = (int)(nextr() % 300);
        int crlf = (int)(nextr() & 1);
        int fold = (int)(nextr() % 3 == 0);
        size_t L; char *s = gen_msg(nhdr, valw, crlf, fold, &L);
        total++;
        if (msg_match(s, L)) ok++;
        else printf("  GEN FAIL t=%d nhdr=%d valw=%d crlf=%d fold=%d\n", t, nhdr, valw, crlf, fold);
        free(s);
    }

    /* values long enough to overflow the 1024-byte value field (truncation parity) */
    {
        size_t L; char *s = gen_msg(4, 4000, 1, 0, &L);
        total++;
        if (msg_match(s, L)) ok++; else printf("  BIGVAL FAIL\n");
        free(s);
    }
    /* more headers than NEVERC_MAIL_MAX_HEADERS (cap parity) */
    {
        size_t L; char *s = gen_msg(200, 20, 1, 0, &L);
        total++;
        if (msg_match(s, L)) ok++; else printf("  MAXHDR FAIL\n");
        free(s);
    }

    printf("edge cases: %d/%d identical\n", ok, total);
    return ok == total;
}

/* ============================================================
 * Timing
 * ============================================================ */
static void bench(const char *label, const char *data, size_t len, const char *note) {
    if (!msg_match(data, len)) { printf("%-16s  CORRECTNESS FAIL\n", label); return; }
    int iters = (int)(200000000ULL / (len + 1));
    if (iters < 200) iters = 200;

    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { old_parse_message(data, len, &MO); sink = (size_t)MO.header_count; }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { neverc_mail_parse_message(data, len, &MN); sink = (size_t)MN.header_count; }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-16s  %9.1f ms  %9.1f ms  %6.2fx   %s\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, note);
}

int main(void) {
    printf("=== net/mail parse_message: bounded memchr scans (new) vs byte loops (old) ===\n");

    int all_ok = correctness();

#ifndef MAIL_NO_TIMING
    printf("\n%-16s  %9s  %9s  %8s\n", "case", "old", "new", "speedup");

    const char *realistic =
        "Return-Path: <sender@example.com>\r\n"
        "Received: from mail.example.com (mail.example.com [192.0.2.1])\r\n"
        " by mx.example.net with ESMTP id abc123\r\n"
        " for <user@example.net>; Sun, 14 Jun 2026 03:00:00 +0000\r\n"
        "From: \"Sender Name\" <sender@example.com>\r\n"
        "To: user@example.net\r\n"
        "Subject: A reasonably typical subject line\r\n"
        "Date: Sun, 14 Jun 2026 03:00:00 +0000\r\n"
        "Message-ID: <unique-id-12345@example.com>\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "\r\n"
        "Hello, this is the message body.\r\n";
    bench("realistic", realistic, strlen(realistic), "(10 headers)");

    size_t L;
    char *many = gen_msg(60, 60, 1, 0, &L);
    bench("many_headers", many, L, "(60 headers x ~60B)");
    free(many);

    char *longv = gen_msg(8, 900, 1, 0, &L);
    bench("long_values", longv, L, "(8 headers x ~900B)");
    free(longv);

    char *folded = gen_msg(20, 400, 1, 1, &L);
    bench("folded", folded, L, "(20 headers, continuation)");
    free(folded);
#endif

    printf("\n=== Done%s ===\n", all_ok ? "" : " (MISMATCH!)");
    return all_ok ? 0 : 1;
}
