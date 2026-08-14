/*
 * Poly1305 MAC — RFC 7539.
 * p = 2^130 - 5.
 *
 * On 64-bit targets (where __int128 is available) we use the "donna-64"
 * representation: 3 limbs of 44/44/42 bits with 128-bit products. This roughly
 * halves the multiply count per block versus the portable 32-bit (5x26) path,
 * which is kept as a fallback for platforms without __int128.
 */
#include "neverc/std/crypto/poly1305.h"
#include "neverc/std/crypto/subtle.h"
#include <string.h>

#if defined(__SIZEOF_INT128__)

/* ───────────────────────── donna-64 (64-bit limbs) ─────────────────────── */

typedef unsigned __int128 nci_u128;

static uint64_t get_u64le(const uint8_t *p) {
    return (uint64_t)p[0]        | ((uint64_t)p[1] << 8)  |
           ((uint64_t)p[2] << 16)| ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32)| ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48)| ((uint64_t)p[7] << 56);
}

void neverc_poly1305_auth(uint8_t tag[16], const uint8_t *msg, size_t msg_len,
                          const uint8_t key[32]) {
    uint64_t t0 = get_u64le(key + 0);
    uint64_t t1 = get_u64le(key + 8);

    /* Clamp r and split into 44/44/42-bit limbs. */
    uint64_t r0 = ( t0                    ) & 0xffc0fffffffULL;
    uint64_t r1 = ((t0 >> 44) | (t1 << 20)) & 0xfffffc0ffffULL;
    uint64_t r2 = ((t1 >> 24)             ) & 0x00ffffffc0fULL;

    uint64_t s1 = r1 * (5 << 2);
    uint64_t s2 = r2 * (5 << 2);

    uint64_t h0 = 0, h1 = 0, h2 = 0;
    uint64_t c;

    size_t off = 0;
    while (off < msg_len) {
        size_t blen = msg_len - off;
        uint64_t m0, m1, hibit;
        if (blen >= 16) {
            blen = 16;
            m0 = get_u64le(msg + off);
            m1 = get_u64le(msg + off + 8);
            hibit = (uint64_t)1 << 40; /* 2^128 in limb2 */
        } else {
            uint8_t buf[16];
            memset(buf, 0, sizeof(buf));
            memcpy(buf, msg + off, blen);
            buf[blen] = 1; /* high bit for final partial block */
            m0 = get_u64le(buf);
            m1 = get_u64le(buf + 8);
            hibit = 0;
        }

        /* h += m */
        h0 += ( m0                    ) & 0xfffffffffffULL;
        h1 += ((m0 >> 44) | (m1 << 20)) & 0xfffffffffffULL;
        h2 += (((m1 >> 24)            ) & 0x3ffffffffffULL) | hibit;

        /* h *= r  (mod 2^130 - 5) */
        nci_u128 d, d0, d1, d2;
        d0 = (nci_u128)h0 * r0; d = (nci_u128)h1 * s2; d0 += d; d = (nci_u128)h2 * s1; d0 += d;
        d1 = (nci_u128)h0 * r1; d = (nci_u128)h1 * r0; d1 += d; d = (nci_u128)h2 * s2; d1 += d;
        d2 = (nci_u128)h0 * r2; d = (nci_u128)h1 * r1; d2 += d; d = (nci_u128)h2 * r0; d2 += d;

        /* partial reduction */
                     c = (uint64_t)(d0 >> 44); h0 = (uint64_t)d0 & 0xfffffffffffULL;
        d1 += c;     c = (uint64_t)(d1 >> 44); h1 = (uint64_t)d1 & 0xfffffffffffULL;
        d2 += c;     c = (uint64_t)(d2 >> 42); h2 = (uint64_t)d2 & 0x3ffffffffffULL;
        h0 += c * 5; c = (h0 >> 44);           h0 = h0 & 0xfffffffffffULL;
        h1 += c;

        off += (blen >= 16) ? 16 : msg_len;
    }

    /* fully carry h */
                 c = (h1 >> 44); h1 &= 0xfffffffffffULL;
    h2 += c;     c = (h2 >> 42); h2 &= 0x3ffffffffffULL;
    h0 += c * 5; c = (h0 >> 44); h0 &= 0xfffffffffffULL;
    h1 += c;     c = (h1 >> 44); h1 &= 0xfffffffffffULL;
    h2 += c;     c = (h2 >> 42); h2 &= 0x3ffffffffffULL;
    h0 += c * 5; c = (h0 >> 44); h0 &= 0xfffffffffffULL;
    h1 += c;

    /* compute h + -p */
    uint64_t g0 = h0 + 5; c = (g0 >> 44); g0 &= 0xfffffffffffULL;
    uint64_t g1 = h1 + c; c = (g1 >> 44); g1 &= 0xfffffffffffULL;
    uint64_t g2 = h2 + c - ((uint64_t)1 << 42);

    /* select h if h < p, else h + -p */
    c = (g2 >> 63) - 1;
    g0 &= c; g1 &= c; g2 &= c;
    c = ~c;
    h0 = (h0 & c) | g0;
    h1 = (h1 & c) | g1;
    h2 = (h2 & c) | g2;

    /* h = (h + pad) */
    t0 = get_u64le(key + 16);
    t1 = get_u64le(key + 24);

    h0 += (( t0                    ) & 0xfffffffffffULL)    ; c = (h0 >> 44); h0 &= 0xfffffffffffULL;
    h1 += (((t0 >> 44) | (t1 << 20)) & 0xfffffffffffULL) + c; c = (h1 >> 44); h1 &= 0xfffffffffffULL;
    h2 += (((t1 >> 24)             ) & 0x3ffffffffffULL) + c;                  h2 &= 0x3ffffffffffULL;

    /* mac = h % 2^128 */
    h0 = (h0      ) | (h1 << 44);
    h1 = (h1 >> 20) | (h2 << 24);

    tag[0]  = (uint8_t)(h0);       tag[1]  = (uint8_t)(h0 >> 8);
    tag[2]  = (uint8_t)(h0 >> 16); tag[3]  = (uint8_t)(h0 >> 24);
    tag[4]  = (uint8_t)(h0 >> 32); tag[5]  = (uint8_t)(h0 >> 40);
    tag[6]  = (uint8_t)(h0 >> 48); tag[7]  = (uint8_t)(h0 >> 56);
    tag[8]  = (uint8_t)(h1);       tag[9]  = (uint8_t)(h1 >> 8);
    tag[10] = (uint8_t)(h1 >> 16); tag[11] = (uint8_t)(h1 >> 24);
    tag[12] = (uint8_t)(h1 >> 32); tag[13] = (uint8_t)(h1 >> 40);
    tag[14] = (uint8_t)(h1 >> 48); tag[15] = (uint8_t)(h1 >> 56);
}

#else

/* ─────────────────────── portable 32-bit (5x26) path ───────────────────── */

static uint32_t get_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) |
           ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

void neverc_poly1305_auth(uint8_t tag[16], const uint8_t *msg, size_t msg_len,
                          const uint8_t key[32]) {
    /* Clamp r */
    uint32_t r0 = get_u32le(key + 0)  & 0x0FFFFFFF;
    uint32_t r1 = get_u32le(key + 4)  & 0x0FFFFFFC;
    uint32_t r2 = get_u32le(key + 8)  & 0x0FFFFFFC;
    uint32_t r3 = get_u32le(key + 12) & 0x0FFFFFFC;

    uint32_t s0 = get_u32le(key + 16);
    uint32_t s1 = get_u32le(key + 20);
    uint32_t s2 = get_u32le(key + 24);
    uint32_t s3 = get_u32le(key + 28);

    /* 130-bit accumulator in 5 limbs of 26 bits */
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

    /* r in 5 limbs */
    uint32_t rr0 = r0 & 0x3ffffff;
    uint32_t rr1 = ((r0 >> 26) | (r1 << 6)) & 0x3ffffff;
    uint32_t rr2 = ((r1 >> 20) | (r2 << 12)) & 0x3ffffff;
    uint32_t rr3 = ((r2 >> 14) | (r3 << 18)) & 0x3ffffff;
    uint32_t rr4 = (r3 >> 8) & 0x3ffffff;

    uint32_t s1_5 = rr1 * 5, s2_5 = rr2 * 5, s3_5 = rr3 * 5, s4_5 = rr4 * 5;

    size_t off = 0;
    while (off < msg_len) {
        /* Load 16-byte block */
        uint8_t block[17];
        size_t blen = msg_len - off;
        if (blen > 16) blen = 16;
        memcpy(block, msg + off, blen);
        block[blen] = 1; /* hibit */
        memset(block + blen + 1, 0, 16 - blen);

        uint32_t t0 = get_u32le(block);
        uint32_t t1 = get_u32le(block + 4);
        uint32_t t2 = get_u32le(block + 8);
        uint32_t t3 = get_u32le(block + 12);

        h0 += t0 & 0x3ffffff;
        h1 += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
        h2 += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
        h3 += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
        h4 += (t3 >> 8);
        if (blen == 16) h4 += (1 << 24);

        /* h *= r (mod 2^130-5) */
        uint64_t d0 = (uint64_t)h0*rr0 + (uint64_t)h1*s4_5 + (uint64_t)h2*s3_5 + (uint64_t)h3*s2_5 + (uint64_t)h4*s1_5;
        uint64_t d1 = (uint64_t)h0*rr1 + (uint64_t)h1*rr0  + (uint64_t)h2*s4_5 + (uint64_t)h3*s3_5 + (uint64_t)h4*s2_5;
        uint64_t d2 = (uint64_t)h0*rr2 + (uint64_t)h1*rr1  + (uint64_t)h2*rr0  + (uint64_t)h3*s4_5 + (uint64_t)h4*s3_5;
        uint64_t d3 = (uint64_t)h0*rr3 + (uint64_t)h1*rr2  + (uint64_t)h2*rr1  + (uint64_t)h3*rr0  + (uint64_t)h4*s4_5;
        uint64_t d4 = (uint64_t)h0*rr4 + (uint64_t)h1*rr3  + (uint64_t)h2*rr2  + (uint64_t)h3*rr1  + (uint64_t)h4*rr0;

        uint32_t c;
        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff; d1 += c;
        c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff; d2 += c;
        c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff; d3 += c;
        c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff; d4 += c;
        c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff; h0 += c * 5;
        c = h0 >> 26;             h0 &= 0x3ffffff; h1 += c;

        off += (blen >= 16) ? 16 : msg_len;
    }

    /* Final reduction: fully carry h */
    uint32_t c = h1 >> 26; h1 &= 0x3ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;

    /* Compute h - p = h - (2^130 - 5) */
    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    uint32_t g4 = h4 + c - (1 << 26);

    /* Select h or g based on whether h >= p */
    uint32_t mask = (g4 >> 31) - 1; /* 0xffffffff if g4 >= 0, 0 if g4 < 0 */
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    /* h = h mod 2^128 */
    uint32_t f0 = h0 | (h1 << 26);
    uint32_t f1 = (h1 >> 6) | (h2 << 20);
    uint32_t f2 = (h2 >> 12) | (h3 << 14);
    uint32_t f3 = (h3 >> 18) | (h4 << 8);

    /* tag = (h + s) mod 2^128 */
    uint64_t t = (uint64_t)f0 + s0; f0 = (uint32_t)t; t >>= 32;
    t += (uint64_t)f1 + s1; f1 = (uint32_t)t; t >>= 32;
    t += (uint64_t)f2 + s2; f2 = (uint32_t)t; t >>= 32;
    t += (uint64_t)f3 + s3; f3 = (uint32_t)t;

    tag[0]  = (uint8_t)(f0);       tag[1]  = (uint8_t)(f0 >> 8);
    tag[2]  = (uint8_t)(f0 >> 16); tag[3]  = (uint8_t)(f0 >> 24);
    tag[4]  = (uint8_t)(f1);       tag[5]  = (uint8_t)(f1 >> 8);
    tag[6]  = (uint8_t)(f1 >> 16); tag[7]  = (uint8_t)(f1 >> 24);
    tag[8]  = (uint8_t)(f2);       tag[9]  = (uint8_t)(f2 >> 8);
    tag[10] = (uint8_t)(f2 >> 16); tag[11] = (uint8_t)(f2 >> 24);
    tag[12] = (uint8_t)(f3);       tag[13] = (uint8_t)(f3 >> 8);
    tag[14] = (uint8_t)(f3 >> 16); tag[15] = (uint8_t)(f3 >> 24);
}

#endif /* __SIZEOF_INT128__ */

int neverc_poly1305_verify(const uint8_t tag[16], const uint8_t *msg, size_t msg_len,
                           const uint8_t key[32]) {
    uint8_t computed[16];
    neverc_poly1305_auth(computed, msg, msg_len, key);
    return neverc_subtle_constant_time_compare(computed, tag, 16);
}
