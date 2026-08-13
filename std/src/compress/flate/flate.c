/*
 * NeverC compress/flate — DEFLATE compression & decompression (RFC 1951).
 *
 * Compression (level 1-9): zlib-class encoder —
 *   - LZ77 match finding over a 32 KiB window with absolute-position hash
 *     chains (correct past 64 KiB, unlike a 16-bit position scheme), greedy
 *     for levels 1-3 and lazy (deflate_slow) for levels 4-9.
 *   - Per-block entropy coding that picks the cheapest of stored / fixed /
 *     dynamic Huffman, so every block uses codes tailored to its statistics.
 *   - Dynamic Huffman trees built with package-merge, the optimal length-
 *     limited prefix code (15-bit lit/len & dist, 7-bit code-length codes).
 *   Level 0 emits stored blocks only.
 * Decompression: supports stored, fixed Huffman, and dynamic Huffman blocks.
 */

#include "neverc/std/compress/flate.h"
#include <string.h>
#include <stdlib.h>

/* ---- Bit I/O ---- */

typedef struct {
    uint8_t *buf;
    size_t   cap, pos;
    uint32_t bits;
    unsigned nbits;
} flate_bw_t;

static int fbw_byte(flate_bw_t *w, uint8_t b) {
    if (w->pos >= w->cap) return -1;
    w->buf[w->pos++] = b;
    return 0;
}

static int fbw_bits(flate_bw_t *w, uint32_t val, unsigned n) {
    w->bits |= val << w->nbits;
    w->nbits += n;
    while (w->nbits >= 8) {
        if (fbw_byte(w, (uint8_t)w->bits) < 0) return -1;
        w->bits >>= 8;
        w->nbits -= 8;
    }
    return 0;
}

static int fbw_flush(flate_bw_t *w) {
    while (w->nbits > 0) {
        if (fbw_byte(w, (uint8_t)w->bits) < 0) return -1;
        w->bits >>= 8;
        if (w->nbits >= 8) w->nbits -= 8; else w->nbits = 0;
    }
    return 0;
}

static int fbw_align(flate_bw_t *w) {
    if (w->nbits > 0) return fbw_flush(w);
    return 0;
}

typedef struct {
    const uint8_t *buf;
    size_t len, pos;
    uint64_t bits;     /* little-endian bit accumulator (LSB = next bit) */
    unsigned nbits;    /* number of valid bits held in `bits` (0..64) */
} flate_br_t;

/*
 * Refill the bit accumulator. The fast path issues a single unaligned 64-bit
 * little-endian load and advances by whole bytes, leaving 56..63 buffered bits
 * — the libdeflate technique that lets several Huffman symbols be decoded per
 * refill instead of reloading one byte at a time. Re-reading the partially
 * consumed top byte on the next refill is harmless: `|=` rewrites identical
 * bits at identical positions (invariant: pos*8 == consumed + nbits). Near
 * end-of-stream it falls back to a safe byte-at-a-time fill that never reads
 * past `len`.
 */
static void fbr_refill(flate_br_t *r) {
    if (r->pos + 8 <= r->len) {
        uint64_t w;
        memcpy(&w, r->buf + r->pos, 8);
        r->bits |= w << r->nbits;
        unsigned add = (63u - r->nbits) >> 3;   /* whole bytes consumed, 0..7 */
        r->pos += add;
        r->nbits += add << 3;                    /* now 56..63 */
    } else {
        while (r->nbits <= 56 && r->pos < r->len) {
            r->bits |= (uint64_t)r->buf[r->pos++] << r->nbits;
            r->nbits += 8;
        }
    }
}

static int fbr_bits(flate_br_t *r, unsigned n, uint32_t *out) {
    if (r->nbits < n) {
        fbr_refill(r);
        if (r->nbits < n) return -1;
    }
    *out = (uint32_t)(r->bits & (((uint64_t)1 << n) - 1));
    r->bits >>= n;
    r->nbits -= n;
    return 0;
}

/*
 * Drop bits back to a byte boundary and hand the still-buffered whole bytes
 * back to the stream so the direct byte reads of a stored block resume from the
 * correct position. `nbits >> 3` is exactly the count of buffered whole bytes;
 * the partial byte's leftover bits are discarded by the alignment.
 */
static void fbr_align(flate_br_t *r) {
    r->pos -= r->nbits >> 3;
    r->bits = 0;
    r->nbits = 0;
}

/* ---- Fixed Huffman tables (RFC 1951 §3.2.6) ---- */

static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,
    67,83,99,115,131,163,195,227,258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const uint16_t dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/*
 * O(1) length/distance -> code lookups (Go/zlib do the same). emit_block calls
 * these ~4x per match token (frequency, two cost estimates, emit), so the old
 * linear scan over 29/30 entries was a per-token tax on the entropy stage.
 *
 * len_code_tab[length-3] gives the length code index for length in [3,258].
 * For distances, dist_code_tab[d-1] covers d in [1,256]; larger distances reuse
 * the same table via ((d-1)>>7)+14 — the distance-code structure repeats every
 * factor of 128. Both tables are generated from len_base/dist_base and were
 * verified entry-for-entry against the old linear scan over the whole DEFLATE
 * range (lengths 3..258, distances 1..32768).
 */
static const uint8_t len_code_tab[256] = {
    0,1,2,3,4,5,6,7,8,8,9,9,10,10,11,11,
    12,12,12,12,13,13,13,13,14,14,14,14,15,15,15,15,
    16,16,16,16,16,16,16,16,17,17,17,17,17,17,17,17,
    18,18,18,18,18,18,18,18,19,19,19,19,19,19,19,19,
    20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,20,
    21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,21,
    22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,22,
    23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,23,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,24,
    25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,
    25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,
    26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,
    26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,26,
    27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,
    27,27,27,27,27,27,27,27,27,27,27,27,27,27,27,28,
};
static const uint8_t dist_code_tab[256] = {
    0,1,2,3,4,4,5,5,6,6,6,6,7,7,7,7,
    8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,
    10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,10,
    11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
    12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
    12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,12,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,14,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
};

static int len_to_code(unsigned length, unsigned *code, unsigned *extra_bits, unsigned *extra_val) {
    if (length < 3 || length > 258) { *code = *extra_bits = *extra_val = 0; return -1; }
    unsigned i = len_code_tab[length - 3];
    *code = 257 + i;
    *extra_bits = len_extra[i];
    *extra_val = length - len_base[i];
    return 0;
}

static int dist_to_code(unsigned distance, unsigned *code, unsigned *extra_bits, unsigned *extra_val) {
    if (distance < 1 || distance > 32768) { *code = *extra_bits = *extra_val = 0; return -1; }
    unsigned off = distance - 1;
    unsigned i = (off < 256) ? dist_code_tab[off]
                             : (unsigned)dist_code_tab[off >> 7] + 14;
    *code = i;
    *extra_bits = dist_extra[i];
    *extra_val = distance - dist_base[i];
    return 0;
}

/* Encode a fixed Huffman literal/length code (RFC 1951 §3.2.6) */
static int emit_fixed_litlen(flate_bw_t *w, unsigned sym) {
    /* Codes 0-143: 8-bit codes 00110000..10111111 (reversed) */
    /* Codes 144-255: 9-bit codes 110010000..111111111 (reversed) */
    /* Codes 256-279: 7-bit codes 0000000..0010111 (reversed) */
    /* Codes 280-287: 8-bit codes 11000000..11000111 (reversed) */

    uint32_t code; unsigned nbits;
    if (sym <= 143) {
        code = sym + 0x30; nbits = 8;
    } else if (sym <= 255) {
        code = sym - 144 + 0x190; nbits = 9;
    } else if (sym <= 279) {
        code = sym - 256; nbits = 7;
    } else {
        code = sym - 280 + 0xC0; nbits = 8;
    }
    /* reverse bits for DEFLATE (LSB first) */
    uint32_t rev = 0;
    for (unsigned i = 0; i < nbits; i++)
        rev |= ((code >> i) & 1) << (nbits - 1 - i);
    return fbw_bits(w, rev, nbits);
}

static int emit_fixed_dist(flate_bw_t *w, unsigned sym) {
    /* 5-bit codes, reversed */
    uint32_t rev = 0;
    for (unsigned i = 0; i < 5; i++)
        rev |= ((sym >> i) & 1) << (4 - i);
    return fbw_bits(w, rev, 5);
}

/* ---- LZ77 match finding ---- */

#define WINDOW_SIZE  32768
#define MAX_MATCH    258
#define MIN_MATCH    3
#define HASH_BITS    15
#define HASH_SIZE    (1 << HASH_BITS)
#define HASH_MASK    (HASH_SIZE - 1)

static unsigned hash3(const uint8_t *p) {
    return ((unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16)) * 2654435761u >> (32 - HASH_BITS);
}

/* Length of the common prefix of a[] and b[], capped at maxl. Compares 8 bytes
 * per step with a 64-bit load + XOR + count-trailing-zeros (the way zlib and
 * libdeflate scan matches) instead of one byte at a time; the trailing byte
 * loop finishes the final < 8 bytes. Both pointers are guaranteed to have at
 * least maxl readable bytes by the caller, so the 8-byte reads never run past
 * the source buffer (each is gated by ml + 8 <= maxl). */
static unsigned match_len(const uint8_t *a, const uint8_t *b, unsigned maxl) {
    unsigned ml = 0;
    while (ml + 8 <= maxl) {
        uint64_t x, y;
        memcpy(&x, a + ml, 8);
        memcpy(&y, b + ml, 8);
        uint64_t d = x ^ y;
        if (d) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            return ml + (unsigned)(__builtin_clzll(d) >> 3);
#else
            return ml + (unsigned)(__builtin_ctzll(d) >> 3);
#endif
        }
        ml += 8;
    }
    while (ml < maxl && a[ml] == b[ml]) ml++;
    return ml;
}

#define FLATE_BLOCK_TOKENS 16384        /* tokens buffered before a block flush */
#define FLATE_NIL          0xFFFFFFFFu  /* empty hash-chain slot */

/* One LZ77 output token: a literal (dist == 0) or a back-reference. */
typedef struct { uint16_t lit, len, dist; } ftok_t;

/* ---- Optimal length-limited Huffman (package-merge) ---- */

/*
 * Compute canonical Huffman code lengths for freq[0..n-1], each <= maxlen,
 * via the package-merge algorithm (Larmore & Hirschberg) — the optimal
 * length-limited prefix code, matching what zlib/Go target for DEFLATE's
 * 15-bit (lit/len, dist) and 7-bit (code-length) limits. Zero-frequency
 * symbols get length 0. A lone symbol is paired with a dummy so the code is
 * *complete* (DEFLATE rejects an incomplete code-length code). Returns 0 on
 * success, -1 on allocation failure (caller then skips the dynamic block).
 */
static int huff_lengths(const uint32_t *freq, int n, int maxlen, uint8_t *lens) {
    for (int i = 0; i < n; i++) lens[i] = 0;

    int sidx[288], m = 0;
    for (int i = 0; i < n; i++) if (freq[i]) sidx[m++] = i;
    if (m == 0) return 0;
    if (m == 1) {
        int s = sidx[0];
        lens[s] = 1;
        lens[s == 0 ? 1 : 0] = 1;      /* dummy 2nd code -> complete */
        return 0;
    }

    for (int i = 1; i < m; i++) {       /* insertion sort, ascending by freq */
        int k = sidx[i]; uint32_t f = freq[k]; int j = i - 1;
        while (j >= 0 && (freq[sidx[j]] > f || (freq[sidx[j]] == f && sidx[j] > k))) {
            sidx[j + 1] = sidx[j]; j--;
        }
        sidx[j + 1] = k;
    }

    int maxnodes = m * (maxlen + 1) + 8;
    uint64_t *nw = (uint64_t *)malloc((size_t)maxnodes * sizeof(uint64_t));
    int *nc0 = (int *)malloc((size_t)maxnodes * sizeof(int));
    int *nc1 = (int *)malloc((size_t)maxnodes * sizeof(int));
    int *nsy = (int *)malloc((size_t)maxnodes * sizeof(int));
    int *prevl = (int *)malloc((size_t)(2 * m + 4) * sizeof(int));
    int *curl  = (int *)malloc((size_t)(2 * m + 4) * sizeof(int));
    int *cnt   = (int *)calloc((size_t)m, sizeof(int));
    if (!nw || !nc0 || !nc1 || !nsy || !prevl || !curl || !cnt) {
        free(nw); free(nc0); free(nc1); free(nsy); free(prevl); free(curl); free(cnt);
        return -1;
    }

    int np = 0;
    for (int i = 0; i < m; i++) {       /* leaf nodes 0..m-1, ascending */
        nw[np] = freq[sidx[i]]; nc0[np] = -1; nc1[np] = -1; nsy[np] = i;
        prevl[i] = np; np++;
    }
    int prevn = m;

    for (int level = 1; level < maxlen; level++) {
        int npk = prevn / 2, cn = 0, li = 0, pk = 0;
        while (li < m || pk < npk) {    /* merge leaves with packaged pairs */
            uint64_t lwt = (li < m) ? nw[li] : (uint64_t)-1;
            uint64_t pwt = (pk < npk) ? (nw[prevl[2 * pk]] + nw[prevl[2 * pk + 1]])
                                      : (uint64_t)-1;
            if (lwt <= pwt) {
                curl[cn++] = li; li++;
            } else {
                nw[np] = pwt; nc0[np] = prevl[2 * pk]; nc1[np] = prevl[2 * pk + 1];
                nsy[np] = -1; curl[cn++] = np++; pk++;
            }
        }
        for (int i = 0; i < cn; i++) prevl[i] = curl[i];
        prevn = cn;
    }

    int sel = 2 * m - 2;
    if (sel > prevn) sel = prevn;
    for (int i = 0; i < sel; i++) {     /* count leaf occurrences in selection */
        int stack[64], sp = 0;
        stack[sp++] = prevl[i];
        while (sp > 0) {
            int nd = stack[--sp];
            if (nsy[nd] >= 0) cnt[nsy[nd]]++;
            else { stack[sp++] = nc0[nd]; stack[sp++] = nc1[nd]; }
        }
    }
    for (int i = 0; i < m; i++) lens[sidx[i]] = (uint8_t)cnt[i];

    free(nw); free(nc0); free(nc1); free(nsy); free(prevl); free(curl); free(cnt);
    return 0;
}

/* A code is complete iff the Kraft sum equals 1 (no over/under-subscription). */
static int code_complete(const uint8_t *lens, int n, int maxlen) {
    unsigned long total = 0;
    for (int i = 0; i < n; i++) {
        if (lens[i] == 0) continue;
        if (lens[i] > maxlen) return 0;
        total += 1UL << (maxlen - lens[i]);
    }
    return total == (1UL << maxlen);
}

/* Build canonical codes (already bit-reversed for LSB-first DEFLATE packing). */
static void build_enc(const uint8_t *lens, int n, uint16_t *codes) {
    unsigned bl_count[16], next_code[16];
    for (int i = 0; i < 16; i++) bl_count[i] = 0;
    for (int i = 0; i < n; i++) if (lens[i]) bl_count[lens[i]]++;
    unsigned code = 0; bl_count[0] = 0;
    for (int bits = 1; bits <= 15; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }
    for (int i = 0; i < n; i++) {
        unsigned l = lens[i];
        if (!l) { codes[i] = 0; continue; }
        unsigned c = next_code[l]++, rev = 0;
        for (unsigned b = 0; b < l; b++) rev |= ((c >> b) & 1u) << (l - 1 - b);
        codes[i] = (uint16_t)rev;
    }
}

static unsigned fixed_ll_len(unsigned s) {
    if (s <= 143) return 8;
    if (s <= 255) return 9;
    if (s <= 279) return 7;
    return 8;
}

/*
 * Encode one block of tokens, choosing the cheapest of stored / fixed /
 * dynamic Huffman. blk/blk_len give the block's raw bytes (for the stored
 * option). bfinal marks the last block. Returns 0, or -1 if dst overflows.
 */
static int emit_block(flate_bw_t *bw, const ftok_t *toks, int ntok,
                      const uint8_t *blk, size_t blk_len, int bfinal) {
#define WB(v,nb) do { if (fbw_bits(bw, (uint32_t)(v), (unsigned)(nb)) < 0) return -1; } while (0)
    uint32_t lfreq[288], dfreq[30];
    memset(lfreq, 0, sizeof(lfreq));
    memset(dfreq, 0, sizeof(dfreq));
    for (int i = 0; i < ntok; i++) {
        if (toks[i].dist == 0) { lfreq[toks[i].lit]++; continue; }
        unsigned lc, leb, lev, dc, deb, dev;
        len_to_code(toks[i].len, &lc, &leb, &lev);
        dist_to_code(toks[i].dist, &dc, &deb, &dev);
        lfreq[lc]++; dfreq[dc]++;
    }
    lfreq[256]++;                       /* end-of-block */

    uint8_t llen[288], dlen[30];
    int dyn_ok = (huff_lengths(lfreq, 286, 15, llen) == 0)
              && (huff_lengths(dfreq, 30, 15, dlen) == 0);

    int any_dist = 0;
    if (dyn_ok) for (int i = 0; i < 30; i++) if (dlen[i]) { any_dist = 1; break; }
    if (dyn_ok && !any_dist) { dlen[0] = 1; dlen[1] = 1; }  /* complete dummy */

    int hlit = 286; while (hlit > 257 && (!dyn_ok || llen[hlit - 1] == 0)) hlit--;
    int hdist = 30; while (hdist > 1 && (!dyn_ok || dlen[hdist - 1] == 0)) hdist--;

    static const int cl_order[19] =
        {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    uint8_t cllen[19];
    struct { uint8_t sym, eb; uint16_t ev; } rec[286 + 30];
    int nrec = 0, hclen = 4;
    uint64_t dyn_total = (uint64_t)-1;

    if (dyn_ok) {
        uint8_t comb[286 + 30];
        int total = 0;
        for (int i = 0; i < hlit; i++) comb[total++] = llen[i];
        for (int i = 0; i < hdist; i++) comb[total++] = dlen[i];

        uint32_t clfreq[19];
        memset(clfreq, 0, sizeof(clfreq));
        int i = 0;
        while (i < total) {             /* run-length encode the code lengths */
            int cur = comb[i], run = 1;
            while (i + run < total && comb[i + run] == cur) run++;
            if (cur == 0) {
                int rem = run;
                while (rem >= 11) { int r = rem < 138 ? rem : 138;
                    rec[nrec].sym = 18; rec[nrec].eb = 7; rec[nrec].ev = (uint16_t)(r - 11);
                    nrec++; clfreq[18]++; rem -= r; }
                while (rem >= 3) { int r = rem < 10 ? rem : 10;
                    rec[nrec].sym = 17; rec[nrec].eb = 3; rec[nrec].ev = (uint16_t)(r - 3);
                    nrec++; clfreq[17]++; rem -= r; }
                while (rem > 0) {
                    rec[nrec].sym = 0; rec[nrec].eb = 0; rec[nrec].ev = 0;
                    nrec++; clfreq[0]++; rem--; }
            } else {
                rec[nrec].sym = (uint8_t)cur; rec[nrec].eb = 0; rec[nrec].ev = 0;
                nrec++; clfreq[cur]++;
                int rem = run - 1;
                while (rem >= 3) { int r = rem < 6 ? rem : 6;
                    rec[nrec].sym = 16; rec[nrec].eb = 2; rec[nrec].ev = (uint16_t)(r - 3);
                    nrec++; clfreq[16]++; rem -= r; }
                while (rem > 0) {
                    rec[nrec].sym = (uint8_t)cur; rec[nrec].eb = 0; rec[nrec].ev = 0;
                    nrec++; clfreq[cur]++; rem--; }
            }
            i += run;
        }

        if (huff_lengths(clfreq, 19, 7, cllen) != 0) dyn_ok = 0;
        if (dyn_ok && (!code_complete(llen, 286, 15) ||
                       !code_complete(dlen, 30, 15) ||
                       !code_complete(cllen, 19, 7))) dyn_ok = 0;

        if (dyn_ok) {
            hclen = 19; while (hclen > 4 && cllen[cl_order[hclen - 1]] == 0) hclen--;
            uint64_t bits = 3 + 5 + 5 + 4 + 3 * (uint64_t)hclen;
            for (int r = 0; r < nrec; r++) bits += cllen[rec[r].sym] + rec[r].eb;
            bits += llen[256];
            for (int t = 0; t < ntok; t++) {
                if (toks[t].dist == 0) { bits += llen[toks[t].lit]; continue; }
                unsigned lc, leb, lev, dc, deb, dev;
                len_to_code(toks[t].len, &lc, &leb, &lev);
                dist_to_code(toks[t].dist, &dc, &deb, &dev);
                bits += llen[lc] + leb + dlen[dc] + deb;
            }
            dyn_total = bits;
        }
    }

    uint64_t fix_total = 3 + 7;          /* header + fixed EOB (7 bits) */
    for (int t = 0; t < ntok; t++) {
        if (toks[t].dist == 0) { fix_total += fixed_ll_len(toks[t].lit); continue; }
        unsigned lc, leb, lev, dc, deb, dev;
        len_to_code(toks[t].len, &lc, &leb, &lev);
        dist_to_code(toks[t].dist, &dc, &deb, &dev);
        fix_total += fixed_ll_len(lc) + leb + 5 + deb;
    }

    uint64_t stored_total = (blk_len <= 65535)
        ? (3 + 7 + 32 + 8 * (uint64_t)blk_len) : (uint64_t)-1;

    /* ---- Stored ---- */
    if (stored_total <= fix_total && stored_total <= dyn_total) {
        WB(bfinal, 1); WB(0, 2);
        if (fbw_align(bw) < 0) return -1;
        uint16_t len16 = (uint16_t)blk_len, nlen = (uint16_t)~len16;
        if (fbw_byte(bw, (uint8_t)len16) < 0) return -1;
        if (fbw_byte(bw, (uint8_t)(len16 >> 8)) < 0) return -1;
        if (fbw_byte(bw, (uint8_t)nlen) < 0) return -1;
        if (fbw_byte(bw, (uint8_t)(nlen >> 8)) < 0) return -1;
        for (size_t i = 0; i < blk_len; i++)
            if (fbw_byte(bw, blk[i]) < 0) return -1;
        return 0;
    }

    /* ---- Dynamic Huffman ---- */
    if (dyn_ok && dyn_total <= fix_total) {
        uint16_t lcode[288], dcode[30], clcode[19];
        build_enc(llen, 286, lcode);
        build_enc(dlen, 30, dcode);
        build_enc(cllen, 19, clcode);

        WB(bfinal, 1); WB(2, 2);
        WB(hlit - 257, 5); WB(hdist - 1, 5); WB(hclen - 4, 4);
        for (int k = 0; k < hclen; k++) WB(cllen[cl_order[k]], 3);
        for (int r = 0; r < nrec; r++) {
            WB(clcode[rec[r].sym], cllen[rec[r].sym]);
            if (rec[r].eb) WB(rec[r].ev, rec[r].eb);
        }
        for (int t = 0; t < ntok; t++) {
            if (toks[t].dist == 0) { WB(lcode[toks[t].lit], llen[toks[t].lit]); continue; }
            unsigned lc, leb, lev, dc, deb, dev;
            len_to_code(toks[t].len, &lc, &leb, &lev);
            dist_to_code(toks[t].dist, &dc, &deb, &dev);
            WB(lcode[lc], llen[lc]); if (leb) WB(lev, leb);
            WB(dcode[dc], dlen[dc]); if (deb) WB(dev, deb);
        }
        WB(lcode[256], llen[256]);
        return 0;
    }

    /* ---- Fixed Huffman ---- */
    WB(bfinal, 1); WB(1, 2);
    for (int t = 0; t < ntok; t++) {
        if (toks[t].dist == 0) {
            if (emit_fixed_litlen(bw, toks[t].lit) < 0) return -1;
            continue;
        }
        unsigned lc, leb, lev, dc, deb, dev;
        len_to_code(toks[t].len, &lc, &leb, &lev);
        dist_to_code(toks[t].dist, &dc, &deb, &dev);
        if (emit_fixed_litlen(bw, lc) < 0) return -1;
        if (leb) WB(lev, leb);
        if (emit_fixed_dist(bw, dc) < 0) return -1;
        if (deb) WB(dev, deb);
    }
    if (emit_fixed_litlen(bw, 256) < 0) return -1;
    return 0;
#undef WB
}

/* Token accumulator: buffers tokens, flushing a block when full. */
typedef struct {
    flate_bw_t *bw;
    ftok_t     *toks;
    int         ntok;
    const uint8_t *src;
    size_t      block_start, block_bytes;
} flate_enc_t;

static int enc_flush(flate_enc_t *e, int bfinal) {
    if (e->ntok == 0 && !bfinal) return 0;
    const uint8_t *block_src =
        e->src ? e->src + e->block_start : NULL;
    if (emit_block(e->bw, e->toks, e->ntok,
                   block_src, e->block_bytes, bfinal) < 0)
        return -1;
    e->block_start += e->block_bytes;
    e->block_bytes = 0;
    e->ntok = 0;
    return 0;
}

static int enc_lit(flate_enc_t *e, uint8_t b) {
    e->toks[e->ntok].lit = b; e->toks[e->ntok].len = 0; e->toks[e->ntok].dist = 0;
    e->ntok++; e->block_bytes += 1;
    return (e->ntok >= FLATE_BLOCK_TOKENS) ? enc_flush(e, 0) : 0;
}

static int enc_match(flate_enc_t *e, unsigned len, unsigned dist) {
    e->toks[e->ntok].lit = 0;
    e->toks[e->ntok].len = (uint16_t)len;
    e->toks[e->ntok].dist = (uint16_t)dist;
    e->ntok++; e->block_bytes += len;
    return (e->ntok >= FLATE_BLOCK_TOKENS) ? enc_flush(e, 0) : 0;
}

/* Insert position pos into the hash chain (requires pos+MIN_MATCH <= src_len). */
static void hash_insert(uint32_t *head, uint32_t *prev,
                        const uint8_t *src, size_t pos) {
    unsigned h = hash3(src + pos);
    prev[pos & (WINDOW_SIZE - 1)] = head[h];
    head[h] = (uint32_t)pos;
}

/* Longest match for `pos` against the chain (does not insert pos). Reports a
 * match only when it reaches MIN_MATCH; stops early at `nice` or the window. */
static void find_match(const uint8_t *src, size_t src_len, size_t pos,
                       const uint32_t *head, const uint32_t *prev,
                       int max_chain, unsigned nice,
                       unsigned *best_len, unsigned *best_dist) {
    *best_len = 0; *best_dist = 0;
    unsigned h = hash3(src + pos);
    uint32_t cand = head[h];
    unsigned maxl = (unsigned)(src_len - pos);
    if (maxl > MAX_MATCH) maxl = MAX_MATCH;
    unsigned bl = 0, bd = 0;
    int chain = max_chain;
    while (cand != FLATE_NIL && chain-- > 0) {
        size_t c = cand;
        unsigned dist = (unsigned)(pos - c);
        if (dist > WINDOW_SIZE) break;   /* chain is recency-ordered */
        if (bl == 0 || src[c + bl] == src[pos + bl]) {
            unsigned ml = match_len(src + c, src + pos, maxl);
            if (ml > bl) {
                bl = ml; bd = dist;
                if (ml >= maxl || ml >= nice) break;
            }
        }
        cand = prev[c & (WINDOW_SIZE - 1)];
    }
    if (bl >= MIN_MATCH) { *best_len = bl; *best_dist = bd; }
}

/* ---- Compress ---- */

int neverc_flate_compress(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t *dst_len, int level) {
    if (!dst_len || (!src && src_len != 0) ||
        (!dst && *dst_len != 0) || level < 0 || level > 9 ||
        (level > 0 && (uint64_t)src_len > UINT32_MAX))
        return -1;

    flate_bw_t bw = { .buf = dst, .cap = *dst_len, .pos = 0, .bits = 0, .nbits = 0 };

    if (level == 0) {
        /* Stored blocks (no compression) */
        size_t off = 0;
        while (off < src_len) {
            size_t block = src_len - off;
            if (block > 65535) block = 65535;
            int bfinal = (off + block >= src_len) ? 1 : 0;
            if (fbw_bits(&bw, (uint32_t)bfinal, 1) < 0) return -1;
            if (fbw_bits(&bw, 0, 2) < 0) return -1;
            if (fbw_align(&bw) < 0) return -1;

            uint16_t len16 = (uint16_t)block;
            uint16_t nlen = ~len16;
            if (fbw_byte(&bw, (uint8_t)(len16 & 0xFF)) < 0) return -1;
            if (fbw_byte(&bw, (uint8_t)(len16 >> 8)) < 0) return -1;
            if (fbw_byte(&bw, (uint8_t)(nlen & 0xFF)) < 0) return -1;
            if (fbw_byte(&bw, (uint8_t)(nlen >> 8)) < 0) return -1;
            for (size_t i = 0; i < block; i++)
                if (fbw_byte(&bw, src[off + i]) < 0) return -1;
            off += block;
        }
        if (src_len == 0) {
            if (fbw_bits(&bw, 1, 1) < 0) return -1;
            if (fbw_bits(&bw, 0, 2) < 0) return -1;
            if (fbw_align(&bw) < 0) return -1;
            uint16_t z = 0, nz = 0xFFFF;
            if (fbw_byte(&bw, (uint8_t)(z & 0xFF)) < 0) return -1;
            if (fbw_byte(&bw, (uint8_t)(z >> 8)) < 0) return -1;
            if (fbw_byte(&bw, (uint8_t)(nz & 0xFF)) < 0) return -1;
            if (fbw_byte(&bw, (uint8_t)(nz >> 8)) < 0) return -1;
        }
        *dst_len = bw.pos;
        return 0;
    }

    /* LZ77 + adaptive Huffman (levels 1-9). Levels 1-3 are greedy, 4-9 lazy.
     * head/prev hold absolute positions so matching is correct past 64 KiB. */
    static const int  chain_by_level[10] = {0, 4, 8, 32, 16, 32, 128, 256, 1024, 4096};
    static const unsigned nice_by_level[10] = {0, 8, 16, 32, 16, 32, 128, 256, 258, 258};
    int max_chain = chain_by_level[level];
    unsigned nice = nice_by_level[level];
    int use_lazy  = (level >= 4);

    uint32_t *head = (uint32_t *)malloc(HASH_SIZE * sizeof(uint32_t));
    uint32_t *prev = (uint32_t *)malloc(WINDOW_SIZE * sizeof(uint32_t));
    ftok_t   *toks = (ftok_t *)malloc(FLATE_BLOCK_TOKENS * sizeof(ftok_t));
    if (!head || !prev || !toks) { free(head); free(prev); free(toks); return -1; }
    for (int i = 0; i < HASH_SIZE; i++) head[i] = FLATE_NIL;

    flate_enc_t e = { &bw, toks, 0, src, 0, 0 };
    size_t pos = 0;
    unsigned prev_len = 0, prev_dist = 0;
    int have = 0;                        /* a deferred match pending at pos-1 */

    while (pos < src_len) {
        unsigned cl = 0, cd = 0;
        if (pos + MIN_MATCH <= src_len) {
            find_match(src, src_len, pos, head, prev, max_chain, nice, &cl, &cd);
            hash_insert(head, prev, src, pos);
        }

        if (!use_lazy) {                 /* greedy */
            if (cl >= MIN_MATCH) {
                if (enc_match(&e, cl, cd) < 0) goto fail;
                size_t mend = pos + cl;
                for (size_t k = pos + 1; k < mend; k++)
                    if (k + MIN_MATCH <= src_len) hash_insert(head, prev, src, k);
                pos = mend;
            } else {
                if (enc_lit(&e, src[pos]) < 0) goto fail;
                pos++;
            }
            continue;
        }

        if (have) {                      /* lazy: compare deferred vs current */
            if (cl > prev_len) {         /* current wins -> flush deferred literal */
                if (enc_lit(&e, src[pos - 1]) < 0) goto fail;
                prev_len = cl; prev_dist = cd;
                pos++;
            } else {                     /* deferred match wins -> emit it */
                if (enc_match(&e, prev_len, prev_dist) < 0) goto fail;
                size_t mend = (pos - 1) + prev_len;
                for (size_t k = pos + 1; k < mend; k++)
                    if (k + MIN_MATCH <= src_len) hash_insert(head, prev, src, k);
                pos = mend;
                have = 0; prev_len = 0;
            }
        } else {
            if (cl >= MIN_MATCH) { prev_len = cl; prev_dist = cd; have = 1; pos++; }
            else { if (enc_lit(&e, src[pos]) < 0) goto fail; pos++; }
        }
    }
    if (have)                            /* flush a deferred trailing match */
        if (enc_match(&e, prev_len, prev_dist) < 0) goto fail;

    if (enc_flush(&e, 1) < 0) goto fail; /* final block carries BFINAL=1 */
    if (fbw_flush(&bw) < 0) goto fail;

    free(head); free(prev); free(toks);
    *dst_len = bw.pos;
    return 0;

fail:
    free(head); free(prev); free(toks);
    return -1;
}

/* ---- Huffman tree for decompression ---- */

#define HUFFMAX 288

/*
 * Two-level decode table (the libdeflate / zlib-inflate layout). The previous
 * code used one flat 2^15-entry (128 KiB) table: a single load decoded any
 * symbol, but the table blew past L1/L2 and re-initialising up to 32768 entries
 * per dynamic block was itself costly. Here a small HUFF_ROOT_BITS-wide root
 * table resolves every code no longer than the root in one L1-resident load;
 * the few longer codes (>9 bits, rare in real data) point at a compact subtable
 * indexed by the remaining bits. Net effect: the hot path stays in L1, builds
 * touch ~1 KB instead of ~128 KB, and the per-table footprint drops 16x — which
 * also removes a 128 KiB stack object (the code-length table) that threatened
 * small thread stacks on mobile targets.
 *
 * Entry encoding: a FULL entry has len >= 1 (total code length) and sym = the
 * symbol. A LINK entry (root only) has len == 0, sublen >= 1 (the subtable's
 * index width) and sym = the subtable's base offset within table[]. An unused
 * slot is all-zero (len == 0 && sublen == 0) and decodes as an error.
 */
#define HUFF_ROOT_BITS 9
/* > zlib's proven ENOUGH_LENS (852) for root=9, max length 15: a valid DEFLATE
 * code can never need more, and the build guards the bound regardless. */
#define HUFF_TABLE_CAP 2048

typedef struct {
    uint16_t sym;     /* FULL: symbol;  LINK: subtable base offset */
    uint8_t  len;     /* FULL: total code length (>=1);  LINK/empty: 0 */
    uint8_t  sublen;  /* LINK: subtable index-bit count (>=1);  else 0 */
} huff_entry_t;

typedef struct {
    huff_entry_t table[HUFF_TABLE_CAP];
    unsigned root_bits;
    unsigned root_mask;
} huff_table_t;

typedef enum {
    HUFF_CODES,
    HUFF_LENS,
    HUFF_DISTS
} huff_kind_t;

/* Reverse the low `len` bits of `c` (DEFLATE packs codes LSB-first). */
static unsigned huff_rev(unsigned c, unsigned len) {
    unsigned rev = 0;
    for (unsigned b = 0; b < len; b++) rev |= ((c >> b) & 1u) << (len - 1 - b);
    return rev;
}

static int build_huffman(huff_table_t *ht, const uint8_t *lens, int count,
                         huff_kind_t kind) {
    unsigned bl_count[16] = {0};
    unsigned next_code[16] = {0};
    int max_bl = 0;

    for (int i = 0; i < count; i++) {
        if (lens[i] > 15) return -1;
        if (lens[i] > 0) {
            bl_count[lens[i]]++;
            if ((int)lens[i] > max_bl) max_bl = lens[i];
        }
    }

    memset(ht->table, 0, sizeof(ht->table));
    ht->root_bits = 0;
    ht->root_mask = 0;
    if (max_bl == 0)
        return kind == HUFF_DISTS ? 0 : -1;

    int codes_left = 1;
    for (int bits = 1; bits <= max_bl; bits++) {
        codes_left = (codes_left << 1) - (int)bl_count[bits];
        if (codes_left < 0) return -1; /* over-subscribed code lengths */
    }
    if (codes_left > 0 && (kind == HUFF_CODES || max_bl != 1))
        return -1;                    /* invalid incomplete code space */

    unsigned code = 0;
    for (int bits = 1; bits <= max_bl; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    unsigned rb = (max_bl < HUFF_ROOT_BITS) ? (unsigned)max_bl
                                            : (unsigned)HUFF_ROOT_BITS;
    unsigned root_size = 1u << rb;
    ht->root_bits = rb;
    ht->root_mask = root_size - 1;

    /* Pass A: for codes longer than the root, find the longest code sharing each
     * root prefix, then carve out a right-sized subtable and write its LINK. */
    if (max_bl > (int)rb) {
        unsigned sub_max[1u << HUFF_ROOT_BITS];
        for (unsigned p = 0; p < root_size; p++) sub_max[p] = 0;

        unsigned nc[16];
        memcpy(nc, next_code, sizeof(nc));   /* private cursor, leaves Pass B's intact */
        for (int i = 0; i < count; i++) {
            unsigned L = lens[i];
            if (L == 0) continue;
            unsigned c2 = nc[L]++;
            if (L <= rb) continue;
            unsigned prefix = huff_rev(c2, L) & (root_size - 1);
            if (L > sub_max[prefix]) sub_max[prefix] = L;
        }

        unsigned off = root_size;
        for (unsigned p = 0; p < root_size; p++) {
            if (sub_max[p] == 0) continue;
            unsigned sublen = sub_max[p] - rb;
            unsigned ssize = 1u << sublen;
            if (off + ssize > HUFF_TABLE_CAP) return -1;   /* unreachable for valid DEFLATE */
            ht->table[p].sym = (uint16_t)off;              /* subtable base */
            ht->table[p].len = 0;                          /* mark as LINK */
            ht->table[p].sublen = (uint8_t)sublen;
            off += ssize;
        }
    }

    /* Pass B: write FULL entries — short codes into the root, long codes into
     * the subtable their prefix links to. */
    for (int i = 0; i < count; i++) {
        unsigned L = lens[i];
        if (L == 0) continue;
        unsigned rev = huff_rev(next_code[L]++, L);
        if (L <= rb) {
            unsigned step = 1u << L;
            for (unsigned j = rev; j < root_size; j += step) {
                ht->table[j].sym = (uint16_t)i;
                ht->table[j].len = (uint8_t)L;
            }
        } else {
            unsigned prefix = rev & (root_size - 1);
            unsigned base = ht->table[prefix].sym;         /* subtable base from Pass A */
            unsigned subsize = 1u << ht->table[prefix].sublen;
            unsigned step = 1u << (L - rb);
            for (unsigned j = rev >> rb; j < subsize; j += step) {
                ht->table[base + j].sym = (uint16_t)i;
                ht->table[base + j].len = (uint8_t)L;
            }
        }
    }
    return 0;
}

/*
 * Expand one LZ77 back reference: dst[out_pos+k] = dst[out_pos-distance+k] for
 * k in [0,length). Replaces the byte-at-a-time loop the way zlib/libdeflate
 * inflate, picking the cheapest strategy per shape and writing exactly `length`
 * bytes (no output slack, so the caller's bounds check is unchanged):
 *   - distance == 1     : a single repeated byte -> memset.
 *   - distance >= length: source/destination are disjoint; a big run uses the
 *     SIMD libc memcpy, a short run uses inline 8-byte + byte moves so the tiny
 *     copies common in text never pay memcpy's call overhead.
 *   - short overlap     : plain byte copy (cheaper than several tiny memcpys).
 *   - long overlap      : lay down the first `distance`-byte period, then
 *     replicate it from the run's start, doubling the copied span each step
 *     (O(log length) large memcpys instead of O(length) byte stores).
 */
static void copy_match(uint8_t *dst, size_t out_pos,
                       unsigned distance, unsigned length) {
    uint8_t *out = dst + out_pos;
    const uint8_t *from = dst + (out_pos - distance);

    if (distance == 1) { memset(out, from[0], length); return; }

    if (distance >= length) {                 /* disjoint source and dest */
        if (length >= 16) { memcpy(out, from, length); return; }
        unsigned len = length;
        while (len >= 8) { memcpy(out, from, 8); out += 8; from += 8; len -= 8; }
        while (len) { *out++ = *from++; len--; }
        return;
    }

    if (length < 16) {                         /* short overlapping run */
        for (unsigned k = 0; k < length; k++) out[k] = from[k];
        return;
    }

    memcpy(out, from, distance);               /* longer overlap: period doubling */
    size_t filled = distance;
    while (filled < length) {
        size_t chunk = (size_t)length - filled;
        if (chunk > filled) chunk = filled;
        memcpy(out + filled, out, chunk);
        filled += chunk;
    }
}

static int huff_decode(huff_table_t *ht, flate_br_t *r, uint16_t *sym) {
    /* 15 = longest possible DEFLATE code; one refill buffers a whole code (and
     * usually several), and near EOF the len > nbits checks reject truncation. */
    if (r->nbits < 15) fbr_refill(r);
    huff_entry_t e = ht->table[r->bits & ht->root_mask];
    unsigned n = e.len;
    if (n == 0) {                                   /* LINK or empty root slot */
        if (e.sublen == 0) return -1;               /* no code here */
        unsigned si = (unsigned)((r->bits >> ht->root_bits) &
                                 ((1u << e.sublen) - 1));
        e = ht->table[e.sym + si];                  /* e.sym is the subtable base */
        n = e.len;
        if (n == 0) return -1;
    }
    if (n > r->nbits) return -1;
    r->bits >>= n;
    r->nbits -= n;
    *sym = e.sym;
    return 0;
}

/* ---- Decompress ---- */

int neverc_flate_decompress(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len) {
    if (!dst_len || (!src && src_len > 0) || (!dst && *dst_len > 0))
        return -1;
    flate_br_t br = { .buf = src, .len = src_len, .pos = 0, .bits = 0, .nbits = 0 };
    size_t out_pos = 0;
    size_t out_cap = *dst_len;

    /* Allocate the (large) Huffman tables once and reuse them across every
     * block — a long stream is split into many blocks, so a malloc/free per
     * block would repeatedly churn ~128 KiB twice over. */
    huff_table_t *lit_ht = (huff_table_t *)malloc(sizeof(huff_table_t));
    huff_table_t *dist_ht = (huff_table_t *)malloc(sizeof(huff_table_t));
    if (!lit_ht || !dist_ht) { free(lit_ht); free(dist_ht); return -1; }

    for (;;) {
        uint32_t bfinal, btype;
        if (fbr_bits(&br, 1, &bfinal) < 0) goto err;
        if (fbr_bits(&br, 2, &btype) < 0) goto err;

        if (btype == 0) {
            /* Stored block */
            fbr_align(&br);
            if (br.pos > br.len || br.len - br.pos < 4) goto err;
            uint16_t len16 = (uint16_t)br.buf[br.pos] | ((uint16_t)br.buf[br.pos+1] << 8);
            uint16_t nlen = (uint16_t)br.buf[br.pos+2] | ((uint16_t)br.buf[br.pos+3] << 8);
            br.pos += 4;
            if ((uint16_t)(len16 ^ nlen) != 0xFFFF) goto err;
            if ((size_t)len16 > br.len - br.pos) goto err;
            if ((size_t)len16 > out_cap - out_pos) goto err;
            if (len16 > 0)
                memcpy(dst + out_pos, br.buf + br.pos, len16);
            br.pos += len16;
            out_pos += len16;
        } else if (btype == 1 || btype == 2) {
            if (btype == 1) {
                /* Fixed Huffman codes */
                uint8_t lit_lens[288], dist_lens[32];
                for (int i = 0; i <= 143; i++) lit_lens[i] = 8;
                for (int i = 144; i <= 255; i++) lit_lens[i] = 9;
                for (int i = 256; i <= 279; i++) lit_lens[i] = 7;
                for (int i = 280; i <= 287; i++) lit_lens[i] = 8;
                for (int i = 0; i < 32; i++) dist_lens[i] = 5;
                if (build_huffman(
                        lit_ht, lit_lens, 288, HUFF_LENS) < 0)
                    goto err;
                if (build_huffman(
                        dist_ht, dist_lens, 32, HUFF_DISTS) < 0)
                    goto err;
            } else {
                /* Dynamic Huffman codes */
                uint32_t hlit, hdist, hclen;
                if (fbr_bits(&br, 5, &hlit) < 0) goto err;
                hlit += 257;
                if (fbr_bits(&br, 5, &hdist) < 0) goto err;
                hdist += 1;
                if (fbr_bits(&br, 4, &hclen) < 0) goto err;
                hclen += 4;
                if (hlit > 286 || hdist > 30) goto err;

                static const int cl_order[19] = {
                    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
                };
                uint8_t cl_lens[19];
                memset(cl_lens, 0, sizeof(cl_lens));
                for (unsigned i = 0; i < hclen; i++) {
                    uint32_t v;
                    if (fbr_bits(&br, 3, &v) < 0) goto err;
                    cl_lens[cl_order[i]] = (uint8_t)v;
                }

                huff_table_t cl_ht;
                if (build_huffman(
                        &cl_ht, cl_lens, 19, HUFF_CODES) < 0)
                    goto err;

                unsigned total = (unsigned)(hlit + hdist);
                uint8_t all_lens[288 + 32];
                memset(all_lens, 0, sizeof(all_lens));
                unsigned idx = 0;
                while (idx < total) {
                    uint16_t sym;
                    if (huff_decode(&cl_ht, &br, &sym) < 0) goto err;
                    if (sym < 16) {
                        all_lens[idx++] = (uint8_t)sym;
                    } else if (sym == 16) {
                        if (idx == 0) goto err;
                        uint32_t rep;
                        if (fbr_bits(&br, 2, &rep) < 0) goto err;
                        rep += 3;
                        if (rep > total - idx) goto err;
                        uint8_t prev_len = all_lens[idx - 1];
                        for (uint32_t r = 0; r < rep; r++)
                            all_lens[idx++] = prev_len;
                    } else if (sym == 17) {
                        uint32_t rep;
                        if (fbr_bits(&br, 3, &rep) < 0) goto err;
                        rep += 3;
                        if (rep > total - idx) goto err;
                        for (uint32_t r = 0; r < rep; r++)
                            all_lens[idx++] = 0;
                    } else if (sym == 18) {
                        uint32_t rep;
                        if (fbr_bits(&br, 7, &rep) < 0) goto err;
                        rep += 11;
                        if (rep > total - idx) goto err;
                        for (uint32_t r = 0; r < rep; r++)
                            all_lens[idx++] = 0;
                    } else {
                        goto err;
                    }
                }

                if (all_lens[256] == 0) goto err;
                if (build_huffman(
                        lit_ht, all_lens, (int)hlit, HUFF_LENS) < 0)
                    goto err;
                if (build_huffman(
                        dist_ht, all_lens + hlit, (int)hdist,
                        HUFF_DISTS) < 0)
                    goto err;
            }

            /* Decode block data */
            for (;;) {
                uint16_t sym;
                if (huff_decode(lit_ht, &br, &sym) < 0) goto err;

                if (sym < 256) {
                    if (out_pos >= out_cap) goto err;
                    dst[out_pos++] = (uint8_t)sym;
                } else if (sym == 256) {
                    break;
                } else {
                    /* length */
                    unsigned li = sym - 257;
                    if (li >= 29) goto err;
                    unsigned length = len_base[li];
                    if (len_extra[li] > 0) {
                        uint32_t extra;
                        if (fbr_bits(&br, len_extra[li], &extra) < 0) goto err;
                        length += extra;
                    }

                    /* distance */
                    uint16_t dsym;
                    if (huff_decode(dist_ht, &br, &dsym) < 0) goto err;
                    if (dsym >= 30) goto err;
                    unsigned distance = dist_base[dsym];
                    if (dist_extra[dsym] > 0) {
                        uint32_t extra;
                        if (fbr_bits(&br, dist_extra[dsym], &extra) < 0) goto err;
                        distance += extra;
                    }

                    if (distance > out_pos ||
                        (size_t)length > out_cap - out_pos)
                        goto err;
                    copy_match(dst, out_pos, distance, length);
                    out_pos += length;
                }
            }
        } else {
            goto err;
        }

        if (bfinal) break;
    }

    free(lit_ht);
    free(dist_ht);
    *dst_len = out_pos;
    return 0;

err:
    free(lit_ht);
    free(dist_ht);
    return -1;
}
