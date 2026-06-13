/*
 * A/B benchmark + correctness check: DEFLATE decompression (inflate).
 *
 * old_inflate — the previous library inflate, which expanded every LZ77 back
 *               reference one byte at a time (dst[k] = dst[k-distance]).
 * new (library) neverc_flate_decompress — same decoder, but back references are
 *               expanded with chunked memcpy: a single memcpy for the common
 *               non-overlapping case (distance >= length) and period-doubling
 *               memcpy for overlapping runs (RLE-like distance < length).
 *
 * The old decoder is reproduced verbatim so the benchmark measures the actual
 * old-vs-new copy strategy. Both decoders run over the same library-compressed
 * data and their output is asserted byte-for-byte identical before timing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/compress/flate.h"

/* ============================================================
 * OLD inflate (byte-at-a-time back-reference copy) — verbatim
 * ============================================================ */

static const uint16_t o_len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
    67,83,99,115,131,163,195,227,258
};
static const uint8_t o_len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t o_dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t o_dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

typedef struct {
    const uint8_t *buf;
    size_t len, pos;
    uint32_t bits;
    unsigned nbits;
} o_br_t;

static int o_bits(o_br_t *r, unsigned n, uint32_t *out) {
    while (r->nbits < n) {
        if (r->pos >= r->len) return -1;
        r->bits |= (uint32_t)r->buf[r->pos++] << r->nbits;
        r->nbits += 8;
    }
    *out = r->bits & ((1u << n) - 1);
    r->bits >>= n;
    r->nbits -= n;
    return 0;
}
static void o_align(o_br_t *r) { r->bits = 0; r->nbits = 0; }

typedef struct { uint16_t sym, len; } o_entry_t;
typedef struct { o_entry_t table[1 << 15]; unsigned max_bits; } o_huff_t;

static int o_build(o_huff_t *ht, const uint8_t *lens, int count) {
    unsigned bl_count[16] = {0}, next_code[16]; int max_bl = 0;
    for (int i = 0; i < count; i++)
        if (lens[i] > 0) { bl_count[lens[i]]++; if (lens[i] > max_bl) max_bl = lens[i]; }
    if (max_bl > 15) return -1;
    ht->max_bits = (unsigned)max_bl;
    unsigned code = 0; bl_count[0] = 0;
    for (int bits = 1; bits <= max_bl; bits++) { code = (code + bl_count[bits-1]) << 1; next_code[bits] = code; }
    if (max_bl == 0) return 0;
    unsigned tsize = 1u << max_bl;
    for (unsigned i = 0; i < tsize; i++) { ht->table[i].sym = 0; ht->table[i].len = 0; }
    for (int i = 0; i < count; i++) {
        unsigned len = lens[i]; if (!len) continue;
        unsigned c = next_code[len]++, rev = 0;
        for (unsigned b = 0; b < len; b++) rev |= ((c >> b) & 1) << (len - 1 - b);
        unsigned step = 1u << len;
        for (unsigned j = rev; j < tsize; j += step) { ht->table[j].sym = (uint16_t)i; ht->table[j].len = (uint16_t)len; }
    }
    return 0;
}
static int o_decode(o_huff_t *ht, o_br_t *r, uint16_t *sym) {
    while (r->nbits < ht->max_bits) {
        if (r->pos >= r->len) { if (r->nbits == 0) return -1; break; }
        r->bits |= (uint32_t)r->buf[r->pos++] << r->nbits; r->nbits += 8;
    }
    unsigned idx = r->bits & ((1u << ht->max_bits) - 1);
    o_entry_t e = ht->table[idx];
    if (e.len == 0) return -1;
    r->bits >>= e.len; r->nbits -= e.len; *sym = e.sym;
    return 0;
}

static int old_inflate(const uint8_t *src, size_t src_len, uint8_t *dst, size_t *dst_len) {
    o_br_t br = { src, src_len, 0, 0, 0 };
    size_t out_pos = 0, out_cap = *dst_len;
    o_huff_t *lit_ht = NULL, *dist_ht = NULL;
    for (;;) {
        uint32_t bfinal, btype;
        if (o_bits(&br, 1, &bfinal) < 0) goto err;
        if (o_bits(&br, 2, &btype) < 0) goto err;
        if (btype == 0) {
            o_align(&br);
            if (br.pos + 4 > br.len) goto err;
            uint16_t len16 = (uint16_t)br.buf[br.pos] | ((uint16_t)br.buf[br.pos+1] << 8);
            uint16_t nlen = (uint16_t)br.buf[br.pos+2] | ((uint16_t)br.buf[br.pos+3] << 8);
            br.pos += 4;
            if ((uint16_t)(len16 ^ nlen) != 0xFFFF) goto err;
            if (br.pos + len16 > br.len || out_pos + len16 > out_cap) goto err;
            memcpy(dst + out_pos, br.buf + br.pos, len16);
            br.pos += len16; out_pos += len16;
        } else if (btype == 1 || btype == 2) {
            lit_ht = (o_huff_t *)malloc(sizeof(o_huff_t));
            dist_ht = (o_huff_t *)malloc(sizeof(o_huff_t));
            if (!lit_ht || !dist_ht) goto err;
            if (btype == 1) {
                uint8_t ll[288], dl[32];
                for (int i = 0; i <= 143; i++) ll[i] = 8;
                for (int i = 144; i <= 255; i++) ll[i] = 9;
                for (int i = 256; i <= 279; i++) ll[i] = 7;
                for (int i = 280; i <= 287; i++) ll[i] = 8;
                for (int i = 0; i < 32; i++) dl[i] = 5;
                if (o_build(lit_ht, ll, 288) < 0) goto err;
                if (o_build(dist_ht, dl, 32) < 0) goto err;
            } else {
                uint32_t hlit, hdist, hclen;
                if (o_bits(&br, 5, &hlit) < 0) goto err;  hlit += 257;
                if (o_bits(&br, 5, &hdist) < 0) goto err; hdist += 1;
                if (o_bits(&br, 4, &hclen) < 0) goto err; hclen += 4;
                static const int cl_order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                uint8_t cl_lens[19]; memset(cl_lens, 0, sizeof(cl_lens));
                for (unsigned i = 0; i < hclen; i++) { uint32_t v; if (o_bits(&br, 3, &v) < 0) goto err; cl_lens[cl_order[i]] = (uint8_t)v; }
                o_huff_t *cl_ht = (o_huff_t *)malloc(sizeof(o_huff_t));
                if (!cl_ht) goto err;
                if (o_build(cl_ht, cl_lens, 19) < 0) { free(cl_ht); goto err; }
                unsigned total = hlit + hdist;
                uint8_t all_lens[288 + 32]; memset(all_lens, 0, sizeof(all_lens));
                unsigned idx = 0;
                while (idx < total) {
                    uint16_t sym;
                    if (o_decode(cl_ht, &br, &sym) < 0) { free(cl_ht); goto err; }
                    if (sym < 16) all_lens[idx++] = (uint8_t)sym;
                    else if (sym == 16) { if (idx == 0) { free(cl_ht); goto err; } uint32_t rep; if (o_bits(&br, 2, &rep) < 0) { free(cl_ht); goto err; } rep += 3; uint8_t pl = all_lens[idx-1]; for (uint32_t r = 0; r < rep && idx < total; r++) all_lens[idx++] = pl; }
                    else if (sym == 17) { uint32_t rep; if (o_bits(&br, 3, &rep) < 0) { free(cl_ht); goto err; } rep += 3; for (uint32_t r = 0; r < rep && idx < total; r++) all_lens[idx++] = 0; }
                    else if (sym == 18) { uint32_t rep; if (o_bits(&br, 7, &rep) < 0) { free(cl_ht); goto err; } rep += 11; for (uint32_t r = 0; r < rep && idx < total; r++) all_lens[idx++] = 0; }
                    else { free(cl_ht); goto err; }
                }
                free(cl_ht);
                if (o_build(lit_ht, all_lens, (int)hlit) < 0) goto err;
                if (o_build(dist_ht, all_lens + hlit, (int)hdist) < 0) goto err;
            }
            for (;;) {
                uint16_t sym;
                if (o_decode(lit_ht, &br, &sym) < 0) goto err;
                if (sym < 256) { if (out_pos >= out_cap) goto err; dst[out_pos++] = (uint8_t)sym; }
                else if (sym == 256) break;
                else {
                    unsigned li = sym - 257; if (li >= 29) goto err;
                    unsigned length = o_len_base[li];
                    if (o_len_extra[li] > 0) { uint32_t ex; if (o_bits(&br, o_len_extra[li], &ex) < 0) goto err; length += ex; }
                    uint16_t dsym; if (o_decode(dist_ht, &br, &dsym) < 0) goto err;
                    if (dsym >= 30) goto err;
                    unsigned distance = o_dist_base[dsym];
                    if (o_dist_extra[dsym] > 0) { uint32_t ex; if (o_bits(&br, o_dist_extra[dsym], &ex) < 0) goto err; distance += ex; }
                    if (distance > out_pos || out_pos + length > out_cap) goto err;
                    for (unsigned k = 0; k < length; k++) dst[out_pos + k] = dst[out_pos - distance + k];
                    out_pos += length;
                }
            }
            free(lit_ht); lit_ht = NULL; free(dist_ht); dist_ht = NULL;
        } else goto err;
        if (bfinal) break;
    }
    *dst_len = out_pos; return 0;
err:
    free(lit_ht); free(dist_ht); return -1;
}

/* ============================================================
 * Timing helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile size_t sink;

/* Build a few representative payloads, compress them, then time inflate. */
static void bench_case(const char *label, const uint8_t *data, size_t n) {
    size_t clen = n * 2 + 1024;
    uint8_t *comp = (uint8_t *)malloc(clen);
    if (neverc_flate_compress(data, n, comp, &clen, 6) != 0) { printf("%-22s  compress fail\n", label); free(comp); return; }

    uint8_t *o = (uint8_t *)malloc(n), *nw = (uint8_t *)malloc(n);
    size_t ol = n, nl = n;
    int ro = old_inflate(comp, clen, o, &ol);
    int rn = neverc_flate_decompress(comp, clen, nw, &nl);
    if (ro || rn || ol != n || nl != n || memcmp(o, data, n) || memcmp(nw, data, n)) {
        printf("%-22s  CORRECTNESS FAIL (ro=%d rn=%d ol=%zu nl=%zu)\n", label, ro, rn, ol, nl);
        free(comp); free(o); free(nw); return;
    }

    int iters = (int)(120000000 / (n + 1)); if (iters < 30) iters = 30;
    /* best-of-5 each: the inflate work is deterministic, so the minimum time is
     * the run least perturbed by scheduler/cache noise (a stable lower bound). */
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t l = n; old_inflate(comp, clen, o, &l); sink = l; }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t l = n; neverc_flate_decompress(comp, clen, nw, &l); sink = l; }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }

    printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx   (ratio %.3f)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, (double)clen / n);
    free(comp); free(o); free(nw);
}

int main(void) {
    printf("=== flate inflate: chunked back-ref copy (new) vs byte-at-a-time (old) ===\n");
    printf("%-22s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    size_t n = 200000;
    uint8_t *buf = (uint8_t *)malloc(n);
    srand(42);

    /* RLE-heavy: long runs -> small-distance overlapping matches (period doubling). */
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)('A' + (i / 64) % 3);
    bench_case("rle_runs", buf, n);

    /* Repeated 32-byte block: distance ~32, length up to 258 (overlapping). */
    for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(i % 32);
    bench_case("periodic_32", buf, n);

    /* English-like text: many medium-distance non-overlapping matches. */
    {
        const char *w[] = {"the ","quick ","brown ","fox ","jumps ","over ","lazy ","dog ","and ","then ","runs ","away "};
        size_t j = 0; while (j < n) { const char *s = w[rand() % 12]; size_t l = strlen(s); if (j + l > n) break; memcpy(buf + j, s, l); j += l; }
        bench_case("english_text", buf, j);
    }

    /* Half structured, half random (mixed literals + matches). */
    for (size_t i = 0; i < n/2; i++) buf[i] = (uint8_t)(i % 64);
    for (size_t i = n/2; i < n; i++) buf[i] = (uint8_t)(rand() & 0xFF);
    bench_case("mixed", buf, n);

    /* All-zero: extreme RLE (max-length, distance-1 runs). */
    memset(buf, 0, n);
    bench_case("all_zero", buf, n);

    free(buf);
    printf("\n=== Done ===\n");
    return 0;
}
