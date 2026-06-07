/*
 * NeverC compress/flate — DEFLATE compression & decompression (RFC 1951).
 *
 * Compression: level 0 = stored blocks, level 1-9 = LZ77 + fixed Huffman.
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
    uint32_t bits;
    unsigned nbits;
} flate_br_t;

static int fbr_bit(flate_br_t *r) {
    if (r->nbits == 0) {
        if (r->pos >= r->len) return -1;
        r->bits = r->buf[r->pos++];
        r->nbits = 8;
    }
    int b = r->bits & 1;
    r->bits >>= 1;
    r->nbits--;
    return b;
}

static int fbr_bits(flate_br_t *r, unsigned n, uint32_t *out) {
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

static void fbr_align(flate_br_t *r) {
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

static int len_to_code(unsigned length, unsigned *code, unsigned *extra_bits, unsigned *extra_val) {
    for (int i = 0; i < 29; i++) {
        unsigned base = len_base[i];
        unsigned top = (i < 28) ? len_base[i+1] : 259;
        if (length >= base && length < top) {
            *code = 257 + (unsigned)i;
            *extra_bits = len_extra[i];
            *extra_val = length - base;
            return 0;
        }
    }
    return -1;
}

static int dist_to_code(unsigned distance, unsigned *code, unsigned *extra_bits, unsigned *extra_val) {
    for (int i = 0; i < 30; i++) {
        unsigned base = dist_base[i];
        unsigned top = (i < 29) ? dist_base[i+1] : 32769;
        if (distance >= base && distance < top) {
            *code = (unsigned)i;
            *extra_bits = dist_extra[i];
            *extra_val = distance - base;
            return 0;
        }
    }
    return -1;
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

/* ---- Compress ---- */

int neverc_flate_compress(const uint8_t *src, size_t src_len,
                          uint8_t *dst, size_t *dst_len, int level) {
    if (level < 0 || level > 9) return -1;

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

    /* LZ77 + Fixed Huffman (levels 1-9) */
    int max_chain = (level <= 3) ? 4 : (level <= 6) ? 16 : 128;

    int16_t *head = (int16_t *)calloc(HASH_SIZE, sizeof(int16_t));
    int16_t *prev = (int16_t *)calloc(WINDOW_SIZE, sizeof(int16_t));
    if (!head || !prev) { free(head); free(prev); return -1; }
    for (int i = 0; i < HASH_SIZE; i++) head[i] = -1;

    /* BFINAL=1, BTYPE=01 (fixed Huffman) */
    if (fbw_bits(&bw, 1, 1) < 0) goto fail;
    if (fbw_bits(&bw, 1, 2) < 0) goto fail;

    size_t pos = 0;
    while (pos < src_len) {
        unsigned best_len = 0, best_dist = 0;

        if (pos + MIN_MATCH <= src_len) {
            unsigned h = hash3(src + pos);
            int chain = max_chain;
            int16_t p = head[h];

            while (p >= 0 && chain-- > 0) {
                size_t candidate = (size_t)(uint16_t)p;
                if (candidate < pos) {
                    unsigned dist = (unsigned)(pos - candidate);
                    if (dist <= WINDOW_SIZE) {
                        unsigned maxl = (unsigned)(src_len - pos);
                        if (maxl > MAX_MATCH) maxl = MAX_MATCH;
                        unsigned ml = 0;
                        while (ml < maxl && src[candidate + ml] == src[pos + ml])
                            ml++;
                        if (ml >= MIN_MATCH && ml > best_len) {
                            best_len = ml;
                            best_dist = dist;
                            if (best_len == MAX_MATCH) break;
                        }
                    }
                }
                p = prev[(size_t)(uint16_t)p];
            }

            /* update hash chain */
            prev[pos & (WINDOW_SIZE - 1)] = head[h];
            head[h] = (int16_t)(pos & 0xFFFF);
        }

        if (best_len >= MIN_MATCH) {
            /* emit length code */
            unsigned lcode, lextra_bits, lextra_val;
            if (len_to_code(best_len, &lcode, &lextra_bits, &lextra_val) < 0) goto fail;
            if (emit_fixed_litlen(&bw, lcode) < 0) goto fail;
            if (lextra_bits > 0)
                if (fbw_bits(&bw, lextra_val, lextra_bits) < 0) goto fail;

            /* emit distance code */
            unsigned dcode, dextra_bits, dextra_val;
            if (dist_to_code(best_dist, &dcode, &dextra_bits, &dextra_val) < 0) goto fail;
            if (emit_fixed_dist(&bw, dcode) < 0) goto fail;
            if (dextra_bits > 0)
                if (fbw_bits(&bw, dextra_val, dextra_bits) < 0) goto fail;

            /* update hash for skipped positions */
            for (unsigned k = 1; k < best_len && pos + k + MIN_MATCH <= src_len; k++) {
                unsigned hk = hash3(src + pos + k);
                prev[(pos + k) & (WINDOW_SIZE - 1)] = head[hk];
                head[hk] = (int16_t)((pos + k) & 0xFFFF);
            }
            pos += best_len;
        } else {
            /* emit literal */
            if (emit_fixed_litlen(&bw, src[pos]) < 0) goto fail;
            pos++;
        }
    }

    /* emit end-of-block (code 256) */
    if (emit_fixed_litlen(&bw, 256) < 0) goto fail;
    if (fbw_flush(&bw) < 0) goto fail;

    free(head);
    free(prev);
    *dst_len = bw.pos;
    return 0;

fail:
    free(head);
    free(prev);
    return -1;
}

/* ---- Huffman tree for decompression ---- */

#define HUFFMAX 288

typedef struct {
    uint16_t sym;
    uint16_t len;
} huff_entry_t;

typedef struct {
    huff_entry_t table[1 << 15];
    unsigned max_bits;
} huff_table_t;

static int build_huffman(huff_table_t *ht, const uint8_t *lens, int count) {
    unsigned bl_count[16] = {0};
    unsigned next_code[16];
    int max_bl = 0;

    for (int i = 0; i < count; i++) {
        if (lens[i] > 0) {
            bl_count[lens[i]]++;
            if ((int)lens[i] > max_bl) max_bl = lens[i];
        }
    }
    if (max_bl > 15) return -1;
    ht->max_bits = (unsigned)max_bl;

    unsigned code = 0;
    bl_count[0] = 0;
    for (int bits = 1; bits <= max_bl; bits++) {
        code = (code + bl_count[bits - 1]) << 1;
        next_code[bits] = code;
    }

    if (max_bl == 0) return 0;
    unsigned tsize = 1u << max_bl;
    for (unsigned i = 0; i < tsize; i++) {
        ht->table[i].sym = 0;
        ht->table[i].len = 0;
    }

    for (int i = 0; i < count; i++) {
        unsigned len = lens[i];
        if (len == 0) continue;
        unsigned c = next_code[len]++;
        /* reverse bits */
        unsigned rev = 0;
        for (unsigned b = 0; b < len; b++)
            rev |= ((c >> b) & 1) << (len - 1 - b);
        /* fill table entries */
        unsigned step = 1u << len;
        for (unsigned j = rev; j < tsize; j += step) {
            ht->table[j].sym = (uint16_t)i;
            ht->table[j].len = (uint16_t)len;
        }
    }
    return 0;
}

static int huff_decode(huff_table_t *ht, flate_br_t *r, uint16_t *sym) {
    while (r->nbits < ht->max_bits) {
        if (r->pos >= r->len) {
            if (r->nbits == 0) return -1;
            break;
        }
        r->bits |= (uint32_t)r->buf[r->pos++] << r->nbits;
        r->nbits += 8;
    }
    unsigned idx = r->bits & ((1u << ht->max_bits) - 1);
    huff_entry_t e = ht->table[idx];
    if (e.len == 0) return -1;
    r->bits >>= e.len;
    r->nbits -= e.len;
    *sym = e.sym;
    return 0;
}

/* ---- Decompress ---- */

int neverc_flate_decompress(const uint8_t *src, size_t src_len,
                            uint8_t *dst, size_t *dst_len) {
    flate_br_t br = { .buf = src, .len = src_len, .pos = 0, .bits = 0, .nbits = 0 };
    size_t out_pos = 0;
    size_t out_cap = *dst_len;

    huff_table_t *lit_ht = NULL, *dist_ht = NULL;

    for (;;) {
        uint32_t bfinal, btype;
        if (fbr_bits(&br, 1, &bfinal) < 0) goto err;
        if (fbr_bits(&br, 2, &btype) < 0) goto err;

        if (btype == 0) {
            /* Stored block */
            fbr_align(&br);
            if (br.pos + 4 > br.len) goto err;
            uint16_t len16 = (uint16_t)br.buf[br.pos] | ((uint16_t)br.buf[br.pos+1] << 8);
            uint16_t nlen = (uint16_t)br.buf[br.pos+2] | ((uint16_t)br.buf[br.pos+3] << 8);
            br.pos += 4;
            if ((uint16_t)(len16 ^ nlen) != 0xFFFF) goto err;
            if (br.pos + len16 > br.len) goto err;
            if (out_pos + len16 > out_cap) goto err;
            memcpy(dst + out_pos, br.buf + br.pos, len16);
            br.pos += len16;
            out_pos += len16;
        } else if (btype == 1 || btype == 2) {
            lit_ht = (huff_table_t *)malloc(sizeof(huff_table_t));
            dist_ht = (huff_table_t *)malloc(sizeof(huff_table_t));
            if (!lit_ht || !dist_ht) goto err;

            if (btype == 1) {
                /* Fixed Huffman codes */
                uint8_t lit_lens[288], dist_lens[32];
                for (int i = 0; i <= 143; i++) lit_lens[i] = 8;
                for (int i = 144; i <= 255; i++) lit_lens[i] = 9;
                for (int i = 256; i <= 279; i++) lit_lens[i] = 7;
                for (int i = 280; i <= 287; i++) lit_lens[i] = 8;
                for (int i = 0; i < 32; i++) dist_lens[i] = 5;
                if (build_huffman(lit_ht, lit_lens, 288) < 0) goto err;
                if (build_huffman(dist_ht, dist_lens, 32) < 0) goto err;
            } else {
                /* Dynamic Huffman codes */
                uint32_t hlit, hdist, hclen;
                if (fbr_bits(&br, 5, &hlit) < 0) goto err;  hlit += 257;
                if (fbr_bits(&br, 5, &hdist) < 0) goto err; hdist += 1;
                if (fbr_bits(&br, 4, &hclen) < 0) goto err; hclen += 4;

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
                if (build_huffman(&cl_ht, cl_lens, 19) < 0) goto err;

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
                        uint8_t prev_len = all_lens[idx - 1];
                        for (uint32_t r = 0; r < rep && idx < total; r++)
                            all_lens[idx++] = prev_len;
                    } else if (sym == 17) {
                        uint32_t rep;
                        if (fbr_bits(&br, 3, &rep) < 0) goto err;
                        rep += 3;
                        for (uint32_t r = 0; r < rep && idx < total; r++)
                            all_lens[idx++] = 0;
                    } else if (sym == 18) {
                        uint32_t rep;
                        if (fbr_bits(&br, 7, &rep) < 0) goto err;
                        rep += 11;
                        for (uint32_t r = 0; r < rep && idx < total; r++)
                            all_lens[idx++] = 0;
                    } else {
                        goto err;
                    }
                }

                if (build_huffman(lit_ht, all_lens, (int)hlit) < 0) goto err;
                if (build_huffman(dist_ht, all_lens + hlit, (int)hdist) < 0) goto err;
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

                    if (distance > out_pos || out_pos + length > out_cap) goto err;
                    for (unsigned k = 0; k < length; k++)
                        dst[out_pos + k] = dst[out_pos - distance + k];
                    out_pos += length;
                }
            }

            free(lit_ht); lit_ht = NULL;
            free(dist_ht); dist_ht = NULL;
        } else {
            goto err;
        }

        if (bfinal) break;
    }

    *dst_len = out_pos;
    return 0;

err:
    free(lit_ht);
    free(dist_ht);
    return -1;
}
