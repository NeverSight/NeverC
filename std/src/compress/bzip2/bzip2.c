/*
 * NeverC compress/bzip2 — bzip2 decompression.
 * Mirrors Go compress/bzip2 (decompression only).
 *
 * Implements: stream magic, block headers, Huffman, MTF, IBWT, RLE.
 * Reference: https://en.wikipedia.org/wiki/Bzip2#File_format
 */

#include "neverc/compress/bzip2.h"
#include <stdlib.h>
#include <string.h>

#define BZ_MAX_BLOCK_SIZE 900000
#define BZ_MAX_SELECTORS  18002
#define BZ_N_GROUPS       6
#define BZ_G_SIZE         50
#define BZ_MAX_ALPHA      258
#define BZ_MAX_CODE_LEN   20

typedef struct {
    const uint8_t *buf;
    size_t len, pos;
    uint32_t live;
    uint32_t buff;
} bz_br_t;

static void bz_br_init(bz_br_t *br, const uint8_t *data, size_t len) {
    br->buf = data; br->len = len; br->pos = 0;
    br->live = 0; br->buff = 0;
}

static int bz_br_bits(bz_br_t *br, unsigned n, uint32_t *out) {
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

static int bz_br_bit(bz_br_t *br) {
    uint32_t v;
    if (bz_br_bits(br, 1, &v) < 0) return -1;
    return (int)v;
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

static int huff_decode(bz_br_t *br, const huff_table_t *ht) {
    int zn = ht->min_len;
    int b = bz_br_bit(br);
    if (b < 0) return -1;
    int zvec = b;

    for (int i = 1; i < zn; i++) {
        b = bz_br_bit(br);
        if (b < 0) return -1;
        zvec = (zvec << 1) | b;
    }

    while (zn <= ht->max_len) {
        if (zvec <= ht->limit[zn]) {
            int idx = zvec - ht->base[zn];
            if (idx < 0 || idx >= BZ_MAX_ALPHA) return -1;
            return ht->perm[idx];
        }
        b = bz_br_bit(br);
        if (b < 0) return -1;
        zvec = (zvec << 1) | b;
        zn++;
    }
    return -1;
}

int neverc_bzip2_decompress(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len) {
    bz_br_t br;
    bz_br_init(&br, src, src_len);

    uint32_t magic;
    if (bz_br_bits(&br, 16, &magic) < 0 || magic != 0x425A) return -1;
    uint32_t h;
    if (bz_br_bits(&br, 8, &h) < 0 || h != 'h') return -1;
    uint32_t bsc;
    if (bz_br_bits(&br, 8, &bsc) < 0 || bsc < '1' || bsc > '9') return -1;
    int block_size100k = (int)bsc - '0';

    size_t out_pos = 0;
    size_t out_cap = *dst_len;
    uint32_t *tt = NULL;

    for (;;) {
        uint32_t hdr_hi, hdr_lo;
        if (bz_br_bits(&br, 24, &hdr_hi) < 0) goto err;
        if (bz_br_bits(&br, 24, &hdr_lo) < 0) goto err;

        if (hdr_hi == 0x177245 && hdr_lo == 0x385090) break;
        if (hdr_hi != 0x314159 || hdr_lo != 0x265359) goto err;

        uint32_t block_crc;
        if (bz_br_bits(&br, 32, &block_crc) < 0) goto err;
        (void)block_crc;

        int randomized = bz_br_bit(&br);
        if (randomized < 0 || randomized) goto err;

        uint32_t orig_ptr;
        if (bz_br_bits(&br, 24, &orig_ptr) < 0) goto err;

        uint32_t used_map;
        if (bz_br_bits(&br, 16, &used_map) < 0) goto err;
        uint8_t in_use[256];
        memset(in_use, 0, sizeof(in_use));
        int n_in_use = 0;
        for (int i = 0; i < 16; i++) {
            if (used_map & (1u << (15 - i))) {
                uint32_t sub;
                if (bz_br_bits(&br, 16, &sub) < 0) goto err;
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

        uint32_t n_groups, n_selectors;
        if (bz_br_bits(&br, 3, &n_groups) < 0) goto err;
        if (n_groups < 2 || n_groups > 6) goto err;
        if (bz_br_bits(&br, 15, &n_selectors) < 0) goto err;
        if (n_selectors == 0 || n_selectors > BZ_MAX_SELECTORS) goto err;

        uint8_t selector_list[BZ_MAX_SELECTORS];
        uint8_t mtf_sel[BZ_N_GROUPS];
        for (int i = 0; i < (int)n_groups; i++) mtf_sel[i] = (uint8_t)i;

        for (unsigned i = 0; i < n_selectors; i++) {
            int j = 0;
            for (;;) {
                int b = bz_br_bit(&br);
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

        huff_table_t tables[BZ_N_GROUPS];
        uint8_t tree_lens[BZ_MAX_ALPHA];

        for (unsigned g = 0; g < n_groups; g++) {
            uint32_t curr;
            if (bz_br_bits(&br, 5, &curr) < 0) goto err;
            for (int i = 0; i < alpha_size; i++) {
                for (;;) {
                    if (curr < 1 || curr > 20) goto err;
                    int b = bz_br_bit(&br);
                    if (b < 0) goto err;
                    if (!b) break;
                    b = bz_br_bit(&br);
                    if (b < 0) goto err;
                    if (b) curr--;
                    else curr++;
                }
                tree_lens[i] = (uint8_t)curr;
            }
            if (huff_build(&tables[g], tree_lens, alpha_size) < 0) goto err;
        }

        int block_cap = block_size100k * 100000;
        uint8_t *block = (uint8_t *)malloc((size_t)block_cap + 1);
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
                if (sel_idx >= (int)n_selectors) { free(block); goto err; }
                group_pos = BZ_G_SIZE;
                sel_idx++;
            }
            group_pos--;

            int grp = selector_list[sel_idx - 1];
            int sym = huff_decode(&br, &tables[grp]);
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
                        group_pos = BZ_G_SIZE;
                        sel_idx++;
                    }

                    grp = selector_list[sel_idx - 1];
                    sym = huff_decode(&br, &tables[grp]);
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
                for (int k = s; k > 0; k--) mtf_arr[k] = mtf_arr[k - 1];
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
        for (int i = 0; i < 256; i++) {
            cumul[i] = sum;
            sum += ftab[i];
        }
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
