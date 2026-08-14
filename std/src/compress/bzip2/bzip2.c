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

/* Width of the Huffman prefix lookup table. Codes no longer than BZ_FAST_BITS
 * are decoded with a single table load; longer codes (rare in practice) fall
 * back to the canonical limit/base walk. */
#define BZ_FAST_BITS      12
#define BZ_FAST_SIZE      (1u << BZ_FAST_BITS)
#define BZ_FAST_MASK      (BZ_FAST_SIZE - 1u)

static uint32_t bz_crc32(const uint8_t *data, size_t len) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i] << 24;
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & UINT32_C(0x80000000))
                      ? (crc << 1) ^ UINT32_C(0x04c11db7)
                      : crc << 1;
    }
    return ~crc;
}

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
            uint32_t v = 0;
            if (avail > 0) {
                /* Left-align the available low bits into the n-bit field,
                 * zero-padded on the right. avail >= 1 and n <= 32 keep the
                 * shift < 32, so a uint32_t is never shifted by its full width
                 * (which is undefined); when avail == 0 the result is just 0. */
                v = (uint32_t)(br->acc & (((uint64_t)1 << avail) - 1));
                v <<= (n - avail);
            }
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
    /* Prefix table indexed by the next BZ_FAST_BITS bits. Each entry packs the
     * code length in the high bits and the symbol in the low 9 bits, or 0 when
     * the prefix belongs to a code longer than BZ_FAST_BITS. Storage is owned by
     * the caller (a per-decompress pool), so this is just a view. */
    const uint16_t *fast;
} huff_table_t;

static int huff_build(huff_table_t *ht, const uint8_t *lens, int n_syms,
                      uint16_t *fast) {
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

    /* Prefixes of long codes (and any gaps) stay 0, marking the slow path. */
    memset(fast, 0, sizeof(uint16_t) * BZ_FAST_SIZE);
    ht->fast = fast;

    int code = 0, perm_idx = 0;
    for (int L = minLen; L <= maxLen; L++) {
        int cnt = 0;
        for (int j = 0; j < n_syms; j++)
            if (lens[j] == L) cnt++;
        ht->base[L] = code - perm_idx;
        ht->limit[L] = code + cnt - 1;

        /* Reject over-subscribed (corrupt) code-length sets: the codes assigned
         * at length L must fit in L bits, i.e. code + cnt <= 2^L. Without this
         * guard a malformed stream can push code past BZ_FAST_SIZE and drive an
         * out-of-bounds write while filling the prefix table below. */
        if (cnt > 0 && (long)code + cnt > (1L << L)) return -1;

        /* Fill the prefix table for every code of this length. The symbols of
         * length L occupy perm[perm_idx .. perm_idx+cnt-1], matching canonical
         * code values code .. code+cnt-1. */
        if (L <= BZ_FAST_BITS) {
            int shift = BZ_FAST_BITS - L;
            for (int k = 0; k < cnt; k++) {
                int sym = ht->perm[perm_idx + k];
                uint16_t packed = (uint16_t)((L << 9) | sym);
                unsigned start = (unsigned)(code + k) << shift;
                unsigned end = start + (1u << shift);
                for (unsigned e = start; e < end; e++) fast[e] = packed;
            }
        }

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

    /* Fast path: resolve any code of length <= BZ_FAST_BITS with one load. */
    if (br->nbits >= BZ_FAST_BITS) {
        uint32_t peek = (uint32_t)((br->acc >> (br->nbits - BZ_FAST_BITS)) & BZ_FAST_MASK);
        uint32_t packed = ht->fast[peek];
        if (packed) {
            br->nbits -= (int)(packed >> 9);
            return (int)(packed & 0x1ff);
        }
        /* Longer code: fall through to the canonical walk below. */
    }

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
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0))
        return -1;
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
    uint32_t combined_crc = 0;
    int block_cap = block_size100k * 100000;

    /* One prefix table per group, reused across every block. */
    uint16_t *fast_pool = (uint16_t *)malloc(sizeof(uint16_t) *
                                             ((size_t)BZ_N_GROUPS << BZ_FAST_BITS));
    if (!fast_pool) return -1;

    /* The packed T-vector doubles as the decode buffer: MTF/RLE2 output is
     * written straight into the low bytes, then successor indices are ORed into
     * the high bits. Allocated once and reused across every block. */
    uint32_t *tt = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)block_cap);
    if (!tt) { free(fast_pool); return -1; }

    for (;;) {
        uint32_t hdr_hi = bz_bits(&br, 24);
        uint32_t hdr_lo = bz_bits(&br, 24);
        if (br.eof) goto err;

        if (hdr_hi == 0x177245 && hdr_lo == 0x385090) {
            uint32_t expected_combined_crc = bz_bits(&br, 32);
            if (br.eof || expected_combined_crc != combined_crc) goto err;
            break;
        }
        if (hdr_hi != 0x314159 || hdr_lo != 0x265359) goto err;

        uint32_t expected_block_crc = bz_bits(&br, 32);
        if (br.eof) goto err;
        size_t block_out_start = out_pos;

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
            if (huff_build(&tables[g], tree_lens, alpha_size,
                           fast_pool + ((size_t)g << BZ_FAST_BITS)) < 0) goto err;
        }

        int nblock = 0;

        /* Move-to-front list of the in-use byte values. A move-to-front of the
         * element at position s is a shift-by-one (memmove), which the platform
         * implements as a branchless SIMD copy — measurably faster here than a
         * bucketed mtfa scheme, whose data-dependent inner loops mispredict on
         * the uniform-random positions seen in high-entropy data. */
        uint8_t mtf_arr[256];
        int nm = 0;
        for (int i = 0; i < 256; i++)
            if (in_use[i]) mtf_arr[nm++] = (uint8_t)i;

        /* cftab[c+1] accumulates the number of byte c as the MTF/RLE2 output is
         * produced; a post-decode prefix sum then turns cftab[c] into the first
         * sorted-order slot for byte value c. */
        uint32_t cftab[257];
        memset(cftab, 0, sizeof(cftab));

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
                /* Fill the run directly into the T-vector's low bytes (high bits
                 * stay 0 for the successor-index OR in the build below). */
                uint32_t *d = tt + nblock;
                uint32_t fillv = ch;
                for (int k = 0; k < run; k++) d[k] = fillv;
                cftab[ch + 1] += (uint32_t)run;
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
                tt[nblock++] = tmp;
                cftab[tmp + 1]++;
            }
        }

        if ((int)orig_ptr >= nblock) goto err;

        /* Build Seward's packed T-vector: tt[j] = (successor_index << 8) | byte.
         * The low bytes already hold the block bytes (written during decode), so
         * this pass only ORs each successor index into its sorted-order slot — a
         * single random access per byte. Because the |= touches only the high
         * bits, the sequential tt[i] read always observes the original byte. */
        for (int i = 1; i < 257; i++) cftab[i] += cftab[i - 1];
        /* The destination slot tt[cftab[uc]] is a data-dependent random write,
         * but the writes are independent across i, so prefetching the slot a few
         * iterations ahead exposes memory-level parallelism the hardware
         * prefetcher cannot (the address is not a stride). The predicted slot is
         * only approximate (cftab keeps advancing), but a prefetch hint to the
         * wrong-but-nearby line is harmless. */
        enum { BZ_BUILD_PD = 32 };
        for (int i = 0; i < nblock; i++) {
            if (i + BZ_BUILD_PD < nblock)
                __builtin_prefetch(&tt[cftab[(uint8_t)(tt[i + BZ_BUILD_PD] & 0xff)]], 1, 0);
            uint8_t uc = (uint8_t)(tt[i] & 0xff);
            tt[cftab[uc]++] |= (uint32_t)i << 8;
        }

        /* Walk the permutation, applying the final RLE1 decode on the fly.
         * RLE1: a run of >= 4 equal bytes is stored as 4 literal bytes followed
         * by a byte giving the count of additional copies. */
        uint32_t t_pos = tt[orig_ptr] >> 8;
        int rprev = -1, rrun = 0;
        for (int i = 0; i < nblock; i++) {
            uint32_t e = tt[t_pos];
            uint8_t ch = (uint8_t)(e & 0xff);
            t_pos = e >> 8;
            /* The successor slot is loaded again at the top of the next
             * iteration; issuing the prefetch now lets that dependent load
             * overlap this iteration's RLE1 work. It is a measurable win when
             * the permutation chain is cache-resident/structured and neutral
             * when it is fully scattered (the line is needed immediately
             * anyway), so it never regresses the random-access case. */
            __builtin_prefetch(&tt[t_pos], 0, 0);

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

        uint32_t actual_block_crc =
            bz_crc32(dst + block_out_start, out_pos - block_out_start);
        if (actual_block_crc != expected_block_crc) goto err;
        combined_crc = (combined_crc << 1) | (combined_crc >> 31);
        combined_crc ^= actual_block_crc;
    }

    /* Rewind unused prefetched whole bytes. Leftover bits in the last
     * used byte are padding; extra whole bytes after the footer are junk. */
    size_t unused_bytes = (size_t)(br.nbits / 8);
    if (unused_bytes > br.pos) goto err;
    if (br.pos - unused_bytes != br.len) goto err;

    free(tt);
    free(fast_pool);
    *dst_len = out_pos;
    return 0;

err:
    free(tt);
    free(fast_pool);
    return -1;
}
