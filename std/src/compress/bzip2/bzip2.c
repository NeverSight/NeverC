/*
 * NeverC compress/bzip2 — bzip2 decompression.
 * Mirrors Go compress/bzip2 (decompression only).
 *
 * Pipeline (decode order): stream magic, block headers, Huffman, MTF + RLE2,
 * inverse Burrows-Wheeler transform (IBWT), final run-length decode (RLE1).
 * Reference: https://en.wikipedia.org/wiki/Bzip2#File_format
 *
 * Performance notes:
 *  - Bit reader uses a 64-bit accumulator with bulk (multi-byte) refill so the
 *    hot Huffman path extracts bits with a shift/mask instead of a per-bit
 *    function call.
 *  - IBWT uses Seward's packed T-vector: each entry stores the successor index
 *    in the high bits and the output byte in the low 8 bits, so the output walk
 *    performs a single (cache-bound) random load per byte instead of two.
 */

#include "neverc/std/compress/bzip2.h"
#include <stdlib.h>
#include <string.h>

#define BZ_MAX_BLOCK_SIZE 900000
#define BZ_MAX_SELECTORS  18002
#define BZ_N_GROUPS       6
#define BZ_G_SIZE         50
#define BZ_MAX_ALPHA      258
#define BZ_MAX_CODE_LEN   20

/* ---- Bit reader: MSB-first, 64-bit accumulator with bulk refill ---- */
typedef struct {
    const uint8_t *buf;
    size_t len, pos;
    uint64_t acc;   /* valid bits live in the low `nbits` bits of acc */
    int nbits;
    int eof;        /* set when a read could not be fully satisfied */
} bz_br_t;

static void bz_br_init(bz_br_t *br, const uint8_t *data, size_t len) {
    br->buf = data; br->len = len; br->pos = 0;
    br->acc = 0; br->nbits = 0; br->eof = 0;
}

static inline void bz_refill(bz_br_t *br) {
    while (br->nbits <= 56 && br->pos < br->len) {
        br->acc = (br->acc << 8) | (uint64_t)br->buf[br->pos++];
        br->nbits += 8;
    }
}

/* Read n bits (1..32), MSB-first. On underflow, sets eof and returns the
 * available bits zero-padded on the right; subsequent reads return 0. */
static inline uint32_t bz_bits(bz_br_t *br, int n) {
    if (br->nbits < n) {
        bz_refill(br);
        if (br->nbits < n) {
            int avail = br->nbits;
            uint32_t v = avail ? (uint32_t)(br->acc & (((uint64_t)1 << avail) - 1)) : 0;
            v <<= (n - avail);
            br->eof = 1;
            br->nbits = 0; br->acc = 0;
            return v;
        }
    }
    br->nbits -= n;
    return (uint32_t)((br->acc >> br->nbits) & (((uint64_t)1 << n) - 1));
}

typedef struct {
    int min_len, max_len;
    int limit[BZ_MAX_CODE_LEN + 2];
    int base[BZ_MAX_CODE_LEN + 2];
    int perm[BZ_MAX_ALPHA];
} huff_table_t;

static int huff_build(huff_table_t *ht, const uint8_t *lens, int n_syms) {
    int minLen = BZ_MAX_CODE_LEN + 1, maxLen = 0;
    for (int i = 0; i < n_syms; i++) {
        if (lens[i] < minLen) minLen = lens[i];
        if (lens[i] > maxLen) maxLen = lens[i];
    }
    if (minLen < 1 || maxLen > BZ_MAX_CODE_LEN) return -1;
    ht->min_len = minLen;
    ht->max_len = maxLen;

    int idx = 0;
    for (int len = minLen; len <= maxLen; len++)
        for (int i = 0; i < n_syms; i++)
            if (lens[i] == len)
                ht->perm[idx++] = i;

    memset(ht->base, 0, sizeof(ht->base));
    memset(ht->limit, 0, sizeof(ht->limit));

    int code = 0, perm_idx = 0;
    for (int L = minLen; L <= maxLen; L++) {
        int cnt = 0;
        for (int j = 0; j < n_syms; j++)
            if (lens[j] == L) cnt++;
        ht->base[L] = code - perm_idx;
        ht->limit[L] = code + cnt - 1;
        perm_idx += cnt;
        code = (code + cnt) << 1;
    }
    return 0;
}

/* Canonical Huffman decode driven by the fast bit reader. The common path
 * refills once so all bits of the longest possible code are buffered, then
 * peeks/extends the code directly out of the accumulator (no per-bit function
 * call or refill branch). A bit-at-a-time slow path covers the end of stream. */
static inline int huff_decode(bz_br_t *br, const huff_table_t *ht) {
    if (br->nbits < BZ_MAX_CODE_LEN)
        bz_refill(br);

    if (br->nbits >= ht->max_len) {
        int zn = ht->min_len;
        int consumed = zn;
        uint32_t zvec = (uint32_t)((br->acc >> (br->nbits - zn)) &
                                   (((uint32_t)1 << zn) - 1));
        for (;;) {
            if ((int)zvec <= ht->limit[zn]) {
                br->nbits -= consumed;
                int idx = (int)zvec - ht->base[zn];
                if (idx < 0 || idx >= BZ_MAX_ALPHA) return -1;
                return ht->perm[idx];
            }
            if (++zn > ht->max_len) return -1;
            uint32_t bit = (uint32_t)((br->acc >> (br->nbits - consumed - 1)) & 1);
            zvec = (zvec << 1) | bit;
            consumed++;
        }
    }

    /* End-of-stream slow path: fewer than max_len bits remain buffered. */
    int zn = ht->min_len;
    uint32_t zvec = bz_bits(br, zn);
    while (zn <= ht->max_len) {
        if (br->eof) return -1;
        if ((int)zvec <= ht->limit[zn]) {
            int idx = (int)zvec - ht->base[zn];
            if (idx < 0 || idx >= BZ_MAX_ALPHA) return -1;
            return ht->perm[idx];
        }
        zvec = (zvec << 1) | bz_bits(br, 1);
        zn++;
    }
    return -1;
}

int neverc_bzip2_decompress(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len) {
    bz_br_t br;
    bz_br_init(&br, src, src_len);

    uint32_t magic = bz_bits(&br, 16);
    if (br.eof || magic != 0x425A) return -1;
    uint32_t h = bz_bits(&br, 8);
    if (br.eof || h != 'h') return -1;
    uint32_t bsc = bz_bits(&br, 8);
    if (br.eof || bsc < '1' || bsc > '9') return -1;
    int block_size100k = (int)bsc - '0';

    size_t out_pos = 0;
    size_t out_cap = *dst_len;
    int block_cap = block_size100k * 100000;
    uint32_t *tt = NULL;
    uint8_t *block = NULL;

    for (;;) {
        uint32_t hdr_hi = bz_bits(&br, 24);
        uint32_t hdr_lo = bz_bits(&br, 24);
        if (br.eof) goto err;

        if (hdr_hi == 0x177245 && hdr_lo == 0x385090) break;
        if (hdr_hi != 0x314159 || hdr_lo != 0x265359) goto err;

        (void)bz_bits(&br, 32);          /* block CRC (unchecked) */

        uint32_t randomized = bz_bits(&br, 1);
        if (br.eof || randomized) goto err;

        uint32_t orig_ptr = bz_bits(&br, 24);
        uint32_t used_map = bz_bits(&br, 16);
        if (br.eof) goto err;
        uint8_t in_use[256];
        memset(in_use, 0, sizeof(in_use));
        int n_in_use = 0;
        for (int i = 0; i < 16; i++) {
            if (used_map & (1u << (15 - i))) {
                uint32_t sub = bz_bits(&br, 16);
                if (br.eof) goto err;
                for (int j = 0; j < 16; j++) {
                    if (sub & (1u << (15 - j))) {
                        in_use[i * 16 + j] = 1;
                        n_in_use++;
                    }
                }
            }
        }
        if (n_in_use == 0) goto err;
        int alpha_size = n_in_use + 2;

        uint32_t n_groups = bz_bits(&br, 3);
        if (br.eof || n_groups < 2 || n_groups > 6) goto err;
        uint32_t n_selectors = bz_bits(&br, 15);
        if (br.eof || n_selectors == 0 || n_selectors > BZ_MAX_SELECTORS) goto err;

        uint8_t selector_list[BZ_MAX_SELECTORS];
        uint8_t mtf_sel[BZ_N_GROUPS];
        for (int i = 0; i < (int)n_groups; i++) mtf_sel[i] = (uint8_t)i;

        for (unsigned i = 0; i < n_selectors; i++) {
            int j = 0;
            for (;;) {
                uint32_t b = bz_bits(&br, 1);
                if (br.eof) goto err;
                if (!b) break;
                j++;
                if (j >= (int)n_groups) goto err;
            }
            uint8_t tmp = mtf_sel[j];
            for (int k = j; k > 0; k--) mtf_sel[k] = mtf_sel[k - 1];
            mtf_sel[0] = tmp;
            selector_list[i] = tmp;
        }

        huff_table_t tables[BZ_N_GROUPS];
        uint8_t tree_lens[BZ_MAX_ALPHA];

        for (unsigned g = 0; g < n_groups; g++) {
            uint32_t curr = bz_bits(&br, 5);
            if (br.eof) goto err;
            for (int i = 0; i < alpha_size; i++) {
                for (;;) {
                    if (curr < 1 || curr > 20) goto err;
                    uint32_t b = bz_bits(&br, 1);
                    if (br.eof) goto err;
                    if (!b) break;
                    b = bz_bits(&br, 1);
                    if (br.eof) goto err;
                    if (b) curr--;
                    else curr++;
                }
                tree_lens[i] = (uint8_t)curr;
            }
            if (huff_build(&tables[g], tree_lens, alpha_size) < 0) goto err;
        }

        block = (uint8_t *)malloc((size_t)block_cap + 1);
        if (!block) goto err;
        int nblock = 0;

        uint8_t mtf_arr[256];
        int nm = 0;
        for (int i = 0; i < 256; i++)
            if (in_use[i]) mtf_arr[nm++] = (uint8_t)i;

        int sel_idx = 0;
        int group_pos = 0;
        int eob = n_in_use + 1;

        for (;;) {
            if (group_pos == 0) {
                if (sel_idx >= (int)n_selectors) goto err;
                group_pos = BZ_G_SIZE;
                sel_idx++;
            }
            group_pos--;

            int grp = selector_list[sel_idx - 1];
            int sym = huff_decode(&br, &tables[grp]);
            if (sym < 0) goto err;

            if (sym == eob) break;

            if (sym == 0 || sym == 1) {
                int run = 0, power = 1;
                for (;;) {
                    run += (sym == 0 ? 1 : 2) * power;
                    power <<= 1;
                    if (run > block_cap) goto err;

                    if (group_pos == 0) {
                        if (sel_idx >= (int)n_selectors) goto err;
                        group_pos = BZ_G_SIZE;
                        sel_idx++;
                    }
                    group_pos--;

                    grp = selector_list[sel_idx - 1];
                    sym = huff_decode(&br, &tables[grp]);
                    if (sym < 0) goto err;
                    if (sym != 0 && sym != 1) break;
                }
                uint8_t ch = mtf_arr[0];
                if (nblock + run > block_cap) goto err;
                memset(block + nblock, ch, (size_t)run);
                nblock += run;
                if (sym == eob) break;
            }

            if (sym >= 2) {
                int s = sym - 1;
                if (s >= nm) goto err;
                uint8_t tmp = mtf_arr[s];
                memmove(&mtf_arr[1], &mtf_arr[0], (size_t)s);
                mtf_arr[0] = tmp;
                if (nblock >= block_cap) goto err;
                block[nblock++] = tmp;
            }
        }

        if ((int)orig_ptr >= nblock) goto err;

        if (!tt) tt = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)block_cap);
        if (!tt) goto err;

        /* Build Seward's packed T-vector: tt[j] = (successor_index << 8) | byte.
         * cftab[c] holds the first sorted-order slot for byte value c. */
        uint32_t cftab[257];
        memset(cftab, 0, sizeof(cftab));
        for (int i = 0; i < nblock; i++) cftab[block[i] + 1]++;
        for (int i = 1; i < 257; i++) cftab[i] += cftab[i - 1];
        for (int i = 0; i < nblock; i++) {
            uint8_t uc = block[i];
            uint32_t j = cftab[uc]++;
            tt[j] = ((uint32_t)i << 8) | block[j];
        }

        free(block);
        block = NULL;

        /* Walk the permutation, applying the final RLE1 decode on the fly.
         * RLE1: a run of >= 4 equal bytes is stored as 4 literal bytes followed
         * by a byte giving the count of additional copies. */
        uint32_t t_pos = tt[orig_ptr] >> 8;
        int rprev = -1, rrun = 0;
        for (int i = 0; i < nblock; i++) {
            uint32_t e = tt[t_pos];
            uint8_t ch = (uint8_t)(e & 0xff);
            t_pos = e >> 8;

            if (rrun == 4) {
                uint32_t extra = ch;
                if (extra > out_cap - out_pos) goto err;
                memset(dst + out_pos, rprev, extra);
                out_pos += extra;
                rrun = 0;
                rprev = -1;
                continue;
            }

            if ((int)ch == rprev) {
                rrun++;
            } else {
                rrun = 1;
                rprev = ch;
            }
            if (out_pos >= out_cap) goto err;
            dst[out_pos++] = ch;
        }
    }

    free(tt);
    free(block);
    *dst_len = out_pos;
    return 0;

err:
    free(tt);
    free(block);
    return -1;
}
