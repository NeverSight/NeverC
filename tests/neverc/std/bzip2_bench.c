/*
 * A/B benchmark + correctness check: bzip2 decompression.
 *
 * Three decoders are used:
 *
 *  - old_bzip2_decompress  — the previous library decoder, reproduced verbatim.
 *      It has a byte-at-a-time bit reader, a two-load inverse-BWT walk, no final
 *      RLE1 stage, and an off-by-one group switch inside RUNA/RUNB runs, so it
 *      silently mis-decodes (or errors on) any real data containing >=4 runs.
 *      Used only by the correctness section to show the shipped decoder failing.
 *
 *  - slow_bzip2_decompress — the same decoding *techniques* as old (byte-at-a-time
 *      reader + two-load IBWT) but with the RLE1 stage and group-switch fix
 *      applied. This is the correct baseline for timing, so the A/B speedup
 *      isolates exactly the new bit reader + packed IBWT (correctness constant).
 *
 *  - neverc_bzip2_decompress (library) — 64-bit bulk-refill bit reader, a single
 *      packed T-vector load per output byte (Seward fast-IBWT), correct RLE1 and
 *      group switching.
 *
 * bzip2 is decompress-only (no compressor), so inputs are produced by piping
 * representative data through the system `bzip2` CLI. Every decode is asserted
 * byte-for-byte against the original before timing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "neverc/std/compress/bzip2.h"

/* ============================================================
 * OLD decoder — verbatim reproduction of the previous library
 * ============================================================ */
#define O_MAX_SELECTORS  18002
#define O_N_GROUPS       6
#define O_G_SIZE         50
#define O_MAX_ALPHA      258
#define O_MAX_CODE_LEN   20

typedef struct {
    const uint8_t *buf;
    size_t len, pos;
    uint32_t live;
    uint32_t buff;
} o_br_t;

static void o_br_init(o_br_t *br, const uint8_t *data, size_t len) {
    br->buf = data; br->len = len; br->pos = 0;
    br->live = 0; br->buff = 0;
}

static int o_br_bits(o_br_t *br, unsigned n, uint32_t *out) {
    *out = 0;
    while (n > 0) {
        if (br->live == 0) {
            if (br->pos >= br->len) return -1;
            br->buff = br->buf[br->pos++];
            br->live = 8;
        }
        unsigned take = n < br->live ? n : br->live;
        *out = (*out << take) | ((br->buff >> (br->live - take)) & ((1u << take) - 1));
        br->live -= take;
        n -= take;
    }
    return 0;
}

static int o_br_bit(o_br_t *br) {
    uint32_t v;
    if (o_br_bits(br, 1, &v) < 0) return -1;
    return (int)v;
}

typedef struct {
    int min_len, max_len;
    int limit[O_MAX_CODE_LEN + 2];
    int base[O_MAX_CODE_LEN + 2];
    int perm[O_MAX_ALPHA];
} o_huff_t;

static int o_huff_build(o_huff_t *ht, const uint8_t *lens, int n_syms) {
    int minLen = O_MAX_CODE_LEN + 1, maxLen = 0;
    for (int i = 0; i < n_syms; i++) {
        if (lens[i] < minLen) minLen = lens[i];
        if (lens[i] > maxLen) maxLen = lens[i];
    }
    if (minLen < 1 || maxLen > O_MAX_CODE_LEN) return -1;
    ht->min_len = minLen; ht->max_len = maxLen;
    int idx = 0;
    for (int len = minLen; len <= maxLen; len++)
        for (int i = 0; i < n_syms; i++)
            if (lens[i] == len) ht->perm[idx++] = i;
    memset(ht->base, 0, sizeof(ht->base));
    memset(ht->limit, 0, sizeof(ht->limit));
    int code = 0, perm_idx = 0;
    for (int L = minLen; L <= maxLen; L++) {
        int cnt = 0;
        for (int j = 0; j < n_syms; j++) if (lens[j] == L) cnt++;
        ht->base[L] = code - perm_idx;
        ht->limit[L] = code + cnt - 1;
        perm_idx += cnt;
        code = (code + cnt) << 1;
    }
    return 0;
}

static int o_huff_decode(o_br_t *br, const o_huff_t *ht) {
    int zn = ht->min_len;
    int b = o_br_bit(br);
    if (b < 0) return -1;
    int zvec = b;
    for (int i = 1; i < zn; i++) {
        b = o_br_bit(br);
        if (b < 0) return -1;
        zvec = (zvec << 1) | b;
    }
    while (zn <= ht->max_len) {
        if (zvec <= ht->limit[zn]) {
            int idx = zvec - ht->base[zn];
            if (idx < 0 || idx >= O_MAX_ALPHA) return -1;
            return ht->perm[idx];
        }
        b = o_br_bit(br);
        if (b < 0) return -1;
        zvec = (zvec << 1) | b;
        zn++;
    }
    return -1;
}

static int old_bzip2_decompress(const uint8_t *src, size_t src_len,
                                uint8_t *dst, size_t *dst_len) {
    o_br_t br;
    o_br_init(&br, src, src_len);
    uint32_t magic;
    if (o_br_bits(&br, 16, &magic) < 0 || magic != 0x425A) return -1;
    uint32_t h;
    if (o_br_bits(&br, 8, &h) < 0 || h != 'h') return -1;
    uint32_t bsc;
    if (o_br_bits(&br, 8, &bsc) < 0 || bsc < '1' || bsc > '9') return -1;
    int block_size100k = (int)bsc - '0';
    size_t out_pos = 0;
    size_t out_cap = *dst_len;
    uint32_t *tt = NULL;
    for (;;) {
        uint32_t hdr_hi, hdr_lo;
        if (o_br_bits(&br, 24, &hdr_hi) < 0) goto err;
        if (o_br_bits(&br, 24, &hdr_lo) < 0) goto err;
        if (hdr_hi == 0x177245 && hdr_lo == 0x385090) break;
        if (hdr_hi != 0x314159 || hdr_lo != 0x265359) goto err;
        uint32_t block_crc;
        if (o_br_bits(&br, 32, &block_crc) < 0) goto err;
        (void)block_crc;
        int randomized = o_br_bit(&br);
        if (randomized < 0 || randomized) goto err;
        uint32_t orig_ptr;
        if (o_br_bits(&br, 24, &orig_ptr) < 0) goto err;
        uint32_t used_map;
        if (o_br_bits(&br, 16, &used_map) < 0) goto err;
        uint8_t in_use[256];
        memset(in_use, 0, sizeof(in_use));
        int n_in_use = 0;
        for (int i = 0; i < 16; i++) {
            if (used_map & (1u << (15 - i))) {
                uint32_t sub;
                if (o_br_bits(&br, 16, &sub) < 0) goto err;
                for (int j = 0; j < 16; j++)
                    if (sub & (1u << (15 - j))) { in_use[i * 16 + j] = 1; n_in_use++; }
            }
        }
        if (n_in_use == 0) goto err;
        int alpha_size = n_in_use + 2;
        uint32_t n_groups, n_selectors;
        if (o_br_bits(&br, 3, &n_groups) < 0) goto err;
        if (n_groups < 2 || n_groups > 6) goto err;
        if (o_br_bits(&br, 15, &n_selectors) < 0) goto err;
        if (n_selectors == 0 || n_selectors > O_MAX_SELECTORS) goto err;
        uint8_t selector_list[O_MAX_SELECTORS];
        uint8_t mtf_sel[O_N_GROUPS];
        for (int i = 0; i < (int)n_groups; i++) mtf_sel[i] = (uint8_t)i;
        for (unsigned i = 0; i < n_selectors; i++) {
            int j = 0;
            for (;;) {
                int b = o_br_bit(&br);
                if (b < 0) goto err;
                if (!b) break;
                j++;
                if (j >= (int)n_groups) goto err;
            }
            uint8_t tmp = mtf_sel[j];
            for (int k = j; k > 0; k--) mtf_sel[k] = mtf_sel[k - 1];
            mtf_sel[0] = tmp;
            selector_list[i] = tmp;
        }
        o_huff_t tables[O_N_GROUPS];
        uint8_t tree_lens[O_MAX_ALPHA];
        for (unsigned g = 0; g < n_groups; g++) {
            uint32_t curr;
            if (o_br_bits(&br, 5, &curr) < 0) goto err;
            for (int i = 0; i < alpha_size; i++) {
                for (;;) {
                    if (curr < 1 || curr > 20) goto err;
                    int b = o_br_bit(&br);
                    if (b < 0) goto err;
                    if (!b) break;
                    b = o_br_bit(&br);
                    if (b < 0) goto err;
                    if (b) curr--; else curr++;
                }
                tree_lens[i] = (uint8_t)curr;
            }
            if (o_huff_build(&tables[g], tree_lens, alpha_size) < 0) goto err;
        }
        int block_cap = block_size100k * 100000;
        uint8_t *block = (uint8_t *)malloc((size_t)block_cap + 1);
        if (!block) goto err;
        int nblock = 0;
        uint8_t mtf_arr[256];
        int nm = 0;
        for (int i = 0; i < 256; i++) if (in_use[i]) mtf_arr[nm++] = (uint8_t)i;
        int sel_idx = 0;
        int group_pos = 0;
        int eob = n_in_use + 1;
        for (;;) {
            if (group_pos == 0) {
                if (sel_idx >= (int)n_selectors) { free(block); goto err; }
                group_pos = O_G_SIZE;
                sel_idx++;
            }
            group_pos--;
            int grp = selector_list[sel_idx - 1];
            int sym = o_huff_decode(&br, &tables[grp]);
            if (sym < 0) { free(block); goto err; }
            if (sym == eob) break;
            if (sym == 0 || sym == 1) {
                int run = 0, power = 1;
                for (;;) {
                    run += (sym == 0 ? 1 : 2) * power;
                    power <<= 1;
                    group_pos--;
                    if (group_pos == 0) {
                        if (sel_idx >= (int)n_selectors) { free(block); goto err; }
                        group_pos = O_G_SIZE;
                        sel_idx++;
                    }
                    grp = selector_list[sel_idx - 1];
                    sym = o_huff_decode(&br, &tables[grp]);
                    if (sym < 0) { free(block); goto err; }
                    if (sym != 0 && sym != 1) break;
                }
                uint8_t ch = mtf_arr[0];
                for (int r = 0; r < run; r++) {
                    if (nblock >= block_cap) { free(block); goto err; }
                    block[nblock++] = ch;
                }
                if (sym == eob) break;
            }
            if (sym >= 2) {
                int s = sym - 1;
                if (s >= nm) { free(block); goto err; }
                uint8_t tmp = mtf_arr[s];
                memmove(&mtf_arr[1], &mtf_arr[0], (size_t)s);
                mtf_arr[0] = tmp;
                if (nblock >= block_cap) { free(block); goto err; }
                block[nblock++] = tmp;
            }
        }
        if ((int)orig_ptr >= nblock) { free(block); goto err; }
        if (!tt) tt = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)block_cap);
        if (!tt) { free(block); goto err; }
        uint32_t ftab[256];
        memset(ftab, 0, sizeof(ftab));
        for (int i = 0; i < nblock; i++) ftab[block[i]]++;
        uint32_t cumul[256];
        uint32_t sum = 0;
        for (int i = 0; i < 256; i++) { cumul[i] = sum; sum += ftab[i]; }
        for (int i = 0; i < nblock; i++) {
            uint8_t ch = block[i];
            tt[cumul[ch]] = (uint32_t)i;
            cumul[ch]++;
        }
        uint32_t t_pos = tt[orig_ptr];
        for (int i = 0; i < nblock; i++) {
            uint8_t ch = block[t_pos];
            t_pos = tt[t_pos];
            if (out_pos >= out_cap) { free(block); goto err; }
            dst[out_pos++] = ch;
        }
        free(block);
    }
    free(tt);
    *dst_len = out_pos;
    return 0;
err:
    free(tt);
    return -1;
}

/* ============================================================
 * NAIVE-BUT-CORRECT baseline: identical decoding *techniques* as the old
 * library (byte-at-a-time bit reader via o_br_t/o_huff_decode, and a two-load
 * inverse-BWT walk), but with the RLE1 stage and the group-switch fix applied.
 * This isolates the timing impact of the new bit reader + packed IBWT while
 * holding correctness constant, so the A/B speedup reflects only those changes.
 * ============================================================ */
static int slow_bzip2_decompress(const uint8_t *src, size_t src_len,
                                 uint8_t *dst, size_t *dst_len) {
    o_br_t br;
    o_br_init(&br, src, src_len);
    uint32_t magic;
    if (o_br_bits(&br, 16, &magic) < 0 || magic != 0x425A) return -1;
    uint32_t h;
    if (o_br_bits(&br, 8, &h) < 0 || h != 'h') return -1;
    uint32_t bsc;
    if (o_br_bits(&br, 8, &bsc) < 0 || bsc < '1' || bsc > '9') return -1;
    int block_size100k = (int)bsc - '0';
    size_t out_pos = 0;
    size_t out_cap = *dst_len;
    int block_cap = block_size100k * 100000;
    uint32_t *tt = NULL;
    for (;;) {
        uint32_t hdr_hi, hdr_lo;
        if (o_br_bits(&br, 24, &hdr_hi) < 0) goto err;
        if (o_br_bits(&br, 24, &hdr_lo) < 0) goto err;
        if (hdr_hi == 0x177245 && hdr_lo == 0x385090) break;
        if (hdr_hi != 0x314159 || hdr_lo != 0x265359) goto err;
        uint32_t block_crc;
        if (o_br_bits(&br, 32, &block_crc) < 0) goto err;
        (void)block_crc;
        int randomized = o_br_bit(&br);
        if (randomized < 0 || randomized) goto err;
        uint32_t orig_ptr;
        if (o_br_bits(&br, 24, &orig_ptr) < 0) goto err;
        uint32_t used_map;
        if (o_br_bits(&br, 16, &used_map) < 0) goto err;
        uint8_t in_use[256];
        memset(in_use, 0, sizeof(in_use));
        int n_in_use = 0;
        for (int i = 0; i < 16; i++) {
            if (used_map & (1u << (15 - i))) {
                uint32_t sub;
                if (o_br_bits(&br, 16, &sub) < 0) goto err;
                for (int j = 0; j < 16; j++)
                    if (sub & (1u << (15 - j))) { in_use[i * 16 + j] = 1; n_in_use++; }
            }
        }
        if (n_in_use == 0) goto err;
        int alpha_size = n_in_use + 2;
        uint32_t n_groups, n_selectors;
        if (o_br_bits(&br, 3, &n_groups) < 0) goto err;
        if (n_groups < 2 || n_groups > 6) goto err;
        if (o_br_bits(&br, 15, &n_selectors) < 0) goto err;
        if (n_selectors == 0 || n_selectors > O_MAX_SELECTORS) goto err;
        uint8_t selector_list[O_MAX_SELECTORS];
        uint8_t mtf_sel[O_N_GROUPS];
        for (int i = 0; i < (int)n_groups; i++) mtf_sel[i] = (uint8_t)i;
        for (unsigned i = 0; i < n_selectors; i++) {
            int j = 0;
            for (;;) {
                int b = o_br_bit(&br);
                if (b < 0) goto err;
                if (!b) break;
                j++;
                if (j >= (int)n_groups) goto err;
            }
            uint8_t tmp = mtf_sel[j];
            for (int k = j; k > 0; k--) mtf_sel[k] = mtf_sel[k - 1];
            mtf_sel[0] = tmp;
            selector_list[i] = tmp;
        }
        o_huff_t tables[O_N_GROUPS];
        uint8_t tree_lens[O_MAX_ALPHA];
        for (unsigned g = 0; g < n_groups; g++) {
            uint32_t curr;
            if (o_br_bits(&br, 5, &curr) < 0) goto err;
            for (int i = 0; i < alpha_size; i++) {
                for (;;) {
                    if (curr < 1 || curr > 20) goto err;
                    int b = o_br_bit(&br);
                    if (b < 0) goto err;
                    if (!b) break;
                    b = o_br_bit(&br);
                    if (b < 0) goto err;
                    if (b) curr--; else curr++;
                }
                tree_lens[i] = (uint8_t)curr;
            }
            if (o_huff_build(&tables[g], tree_lens, alpha_size) < 0) goto err;
        }
        uint8_t *block = (uint8_t *)malloc((size_t)block_cap + 1);
        if (!block) goto err;
        int nblock = 0;
        uint8_t mtf_arr[256];
        int nm = 0;
        for (int i = 0; i < 256; i++) if (in_use[i]) mtf_arr[nm++] = (uint8_t)i;
        int sel_idx = 0;
        int group_pos = 0;
        int eob = n_in_use + 1;
        for (;;) {
            if (group_pos == 0) {
                if (sel_idx >= (int)n_selectors) { free(block); goto err; }
                group_pos = O_G_SIZE;
                sel_idx++;
            }
            group_pos--;
            int grp = selector_list[sel_idx - 1];
            int sym = o_huff_decode(&br, &tables[grp]);
            if (sym < 0) { free(block); goto err; }
            if (sym == eob) break;
            if (sym == 0 || sym == 1) {
                int run = 0, power = 1;
                for (;;) {
                    run += (sym == 0 ? 1 : 2) * power;
                    power <<= 1;
                    if (run > block_cap) { free(block); goto err; }
                    if (group_pos == 0) {          /* check-then-decrement (fixed) */
                        if (sel_idx >= (int)n_selectors) { free(block); goto err; }
                        group_pos = O_G_SIZE;
                        sel_idx++;
                    }
                    group_pos--;
                    grp = selector_list[sel_idx - 1];
                    sym = o_huff_decode(&br, &tables[grp]);
                    if (sym < 0) { free(block); goto err; }
                    if (sym != 0 && sym != 1) break;
                }
                uint8_t ch = mtf_arr[0];
                if (nblock + run > block_cap) { free(block); goto err; }
                memset(block + nblock, ch, (size_t)run);
                nblock += run;
                if (sym == eob) break;
            }
            if (sym >= 2) {
                int s = sym - 1;
                if (s >= nm) { free(block); goto err; }
                uint8_t tmp = mtf_arr[s];
                memmove(&mtf_arr[1], &mtf_arr[0], (size_t)s);
                mtf_arr[0] = tmp;
                if (nblock >= block_cap) { free(block); goto err; }
                block[nblock++] = tmp;
            }
        }
        if ((int)orig_ptr >= nblock) { free(block); goto err; }
        if (!tt) tt = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)block_cap);
        if (!tt) { free(block); goto err; }
        uint32_t ftab[256];
        memset(ftab, 0, sizeof(ftab));
        for (int i = 0; i < nblock; i++) ftab[block[i]]++;
        uint32_t cumul[256];
        uint32_t sum = 0;
        for (int i = 0; i < 256; i++) { cumul[i] = sum; sum += ftab[i]; }
        for (int i = 0; i < nblock; i++) {
            uint8_t ch = block[i];
            tt[cumul[ch]] = (uint32_t)i;
            cumul[ch]++;
        }
        /* two-load IBWT walk + RLE1 decode (naive memory pattern). */
        uint32_t t_pos = tt[orig_ptr];
        int rprev = -1, rrun = 0;
        for (int i = 0; i < nblock; i++) {
            uint8_t ch = block[t_pos];     /* load 1 */
            t_pos = tt[t_pos];             /* load 2 */
            if (rrun == 4) {
                uint32_t extra = ch;
                if (extra > out_cap - out_pos) { free(block); goto err; }
                memset(dst + out_pos, rprev, extra);
                out_pos += extra;
                rrun = 0; rprev = -1;
                continue;
            }
            if ((int)ch == rprev) rrun++; else { rrun = 1; rprev = ch; }
            if (out_pos >= out_cap) { free(block); goto err; }
            dst[out_pos++] = ch;
        }
        free(block);
    }
    free(tt);
    *dst_len = out_pos;
    return 0;
err:
    free(tt);
    return -1;
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile size_t sink;

/* Compress `data` via the system bzip2 CLI (bzip2 is decompress-only here).
 * Returns a malloc'd buffer (caller frees) or NULL on failure. */
static uint8_t *bz_compress(const uint8_t *data, size_t n, int level, size_t *out_len) {
    char inpath[] = "/tmp/ncbzbench_XXXXXX";
    int fd = mkstemp(inpath);
    if (fd < 0) return NULL;
    if (write(fd, data, n) != (ssize_t)n) { close(fd); unlink(inpath); return NULL; }
    close(fd);
    char bzpath[64]; snprintf(bzpath, sizeof bzpath, "%s.bz2", inpath);
    char cmd[256]; snprintf(cmd, sizeof cmd, "bzip2 -%d -c '%s' > '%s'", level, inpath, bzpath);
    if (system(cmd) != 0) { unlink(inpath); return NULL; }
    FILE *f = fopen(bzpath, "rb");
    unlink(inpath); unlink(bzpath);
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (size_t)sz;
    return buf;
}

static void bench_case(const char *label, const uint8_t *data, size_t n, int level) {
    size_t clen;
    uint8_t *comp = bz_compress(data, n, level, &clen);
    if (!comp) { printf("%-22s  compress fail\n", label); return; }

    uint8_t *o = (uint8_t *)malloc(n), *nw = (uint8_t *)malloc(n);
    size_t ol = n, nl = n;
    int ro = slow_bzip2_decompress(comp, clen, o, &ol);
    int rn = neverc_bzip2_decompress(comp, clen, nw, &nl);
    if (rn != 0 || nl != n || memcmp(nw, data, n) != 0 ||
        ro != 0 || ol != n || memcmp(o, data, n) != 0) {
        printf("%-22s  CORRECTNESS FAIL (ro=%d rn=%d ol=%zu nl=%zu)\n", label, ro, rn, ol, nl);
        free(comp); free(o); free(nw); return;
    }

    int iters = (int)(60000000 / (n + 1)); if (iters < 20) iters = 20;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t l = n; slow_bzip2_decompress(comp, clen, o, &l); sink = l; }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { size_t l = n; neverc_bzip2_decompress(comp, clen, nw, &l); sink = l; }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-22s  %8.1f ms  %8.1f ms  %6.2fx   (ratio %.3f)\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, (double)clen / n);
    free(comp); free(o); free(nw);
}

/* Demonstrate the correctness fix: realistic run-bearing data. */
static void correctness_demo(void) {
    printf("\n=== Correctness on realistic (run-bearing) data ===\n");
    printf("%-22s  %10s  %10s\n", "case", "old", "new");
    size_t n = 200000;
    uint8_t *data = (uint8_t *)malloc(n);
    unsigned seed = 999;
    const char *words[] = {"the ","quick ","brown ","fox ","data ","stream "};
    size_t j = 0;
    while (j < n) {
        seed = seed * 1103515245u + 12345u;
        int pick = (seed >> 16) % 10;
        if (pick < 6) { const char *w = words[(seed >> 8) % 6]; size_t l = strlen(w); if (j + l > n) break; memcpy(data + j, w, l); j += l; }
        else { int rl = 4 + ((seed >> 8) % 200); char c = (char)('A' + ((seed >> 4) % 26)); for (int k = 0; k < rl && j < n; k++) data[j++] = (uint8_t)c; }
    }
    n = j;
    size_t clen;
    uint8_t *comp = bz_compress(data, n, 9, &clen);
    if (!comp) { printf("compress fail\n"); free(data); return; }
    uint8_t *o = (uint8_t *)malloc(n + 16), *nw = (uint8_t *)malloc(n + 16);
    size_t ol = n + 16, nl = n + 16;
    int ro = old_bzip2_decompress(comp, clen, o, &ol);
    int rn = neverc_bzip2_decompress(comp, clen, nw, &nl);
    const char *old_res = (ro == 0 && ol == n && memcmp(o, data, n) == 0) ? "correct" :
                          (ro != 0 ? "rc<0 FAIL" : "WRONG OUTPUT");
    const char *new_res = (rn == 0 && nl == n && memcmp(nw, data, n) == 0) ? "correct" : "FAIL";
    printf("%-22s  %10s  %10s   (orig %zu B, .bz2 %zu B)\n", "words+runs 200K", old_res, new_res, n, clen);
    free(comp); free(o); free(nw); free(data);
}

int main(void) {
    printf("=== bzip2 decompress: packed-IBWT + bulk bit reader (new) vs old ===\n");
    printf("%-22s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    size_t n = 400000;
    uint8_t *buf = (uint8_t *)malloc(n);
    unsigned seed = 42;

    /* English-like text, no >=4 runs (old decoder is correct here). */
    {
        const char *w[] = {"the ","quick ","brown ","fox ","jumps ","over ","a ","lazy ","dog ","and ","then ","runs ","far "};
        size_t j = 0;
        while (j < n) { seed = seed*1103515245u+12345u; const char *s = w[(seed>>16)%13]; size_t l = strlen(s); if (j + l > n) break; memcpy(buf + j, s, l); j += l; }
        bench_case("english_text_400K", buf, j, 9);
    }

    /* Pseudo-random bytes broken up to avoid >=4 runs (large alphabet, many
     * Huffman symbols -> stresses the bit reader). */
    {
        seed = 7;
        for (size_t i = 0; i < n; i++) { seed = seed*1103515245u+12345u; uint8_t b = (uint8_t)(seed >> 16); if (i >= 1 && b == buf[i-1]) b ^= 0x5A; buf[i] = b; }
        /* defensively break any residual 3-run into non-equal neighbors */
        for (size_t i = 3; i < n; i++) if (buf[i]==buf[i-1] && buf[i-1]==buf[i-2] && buf[i-2]==buf[i-3]) buf[i] ^= 1;
        bench_case("highentropy_400K", buf, n, 9);
    }

    /* Structured periodic data without 4-runs. */
    {
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)('a' + (i % 23));
        bench_case("periodic23_400K", buf, n, 9);
    }

    free(buf);
    correctness_demo();
    printf("\n=== Done ===\n");
    return 0;
}
