#ifndef NEVERC_AES_CT_FIELD_H
#define NEVERC_AES_CT_FIELD_H

#include <stdint.h>

/* Transpose an 8x8 bit matrix: bit (8*byte + bit) becomes (8*bit + byte).
 * Each stage exchanges one row-coordinate bit with one column-coordinate bit.
 * The same three exchanges undo the transpose. No byte order is assumed. */
static inline uint64_t aes_transpose8(uint64_t x) {
    uint64_t swap = (x ^ (x >> 7)) & UINT64_C(0x00aa00aa00aa00aa);
    x ^= swap ^ (swap << 7);
    swap = (x ^ (x >> 14)) & UINT64_C(0x0000cccc0000cccc);
    x ^= swap ^ (swap << 14);
    swap = (x ^ (x >> 28)) & UINT64_C(0x00000000f0f0f0f0);
    return x ^ swap ^ (swap << 28);
}

/*
 * AES substitution from the field and affine definitions in FIPS 197,
 * sections 4.4, 5.1.1 and 5.3.2:
 * https://doi.org/10.6028/NIST.FIPS.197-upd1
 *
 * Each uint32_t holds one bit plane of 32 independent bytes, least significant
 * plane first. Only fixed Boolean operations act on the data. Inversion uses
 * GF(16)[u]/(u^2 + u + t^3), where GF(16) = GF(2)[t]/(t^4 + t + 1).
 * This reduces byte inversion to four-bit arithmetic without lookup tables.
 */

/* Polynomial multiplication followed by reduction with t^4 = t + 1.
 * All inputs are consumed before dst is written, so aliasing is supported. */
static inline void aes_field_mul4(uint32_t dst[4], const uint32_t a[4],
                                  const uint32_t b[4]) {
    uint32_t p0 = a[0] & b[0];
    uint32_t p1 = (a[0] & b[1]) ^ (a[1] & b[0]);
    uint32_t p2 = (a[0] & b[2]) ^ (a[1] & b[1]) ^ (a[2] & b[0]);
    uint32_t p3 = (a[0] & b[3]) ^ (a[1] & b[2]) ^
                  (a[2] & b[1]) ^ (a[3] & b[0]);
    uint32_t p4 = (a[1] & b[3]) ^ (a[2] & b[2]) ^ (a[3] & b[1]);
    uint32_t p5 = (a[2] & b[3]) ^ (a[3] & b[2]);
    uint32_t p6 = a[3] & b[3];
    dst[0] = p0 ^ p4;
    dst[1] = p1 ^ p4 ^ p5;
    dst[2] = p2 ^ p5 ^ p6;
    dst[3] = p3 ^ p6;
}

/* The four coordinate polynomials of x^14 modulo t^4 + t + 1.
 * For nonzero x this is x^-1; zero maps to zero without a special case. */
static inline void aes_field_inv4(uint32_t q[4]) {
    uint32_t a = q[0], b = q[1], c = q[2], d = q[3];
    uint32_t ab = a & b, ac = a & c, ad = a & d;
    uint32_t bc = b & c, bd = b & d, cd = c & d;
    uint32_t abc = ab & c, abd = ab & d;
    uint32_t acd = ac & d, bcd = bc & d;
    q[0] = a ^ b ^ c ^ ac ^ bc ^ abc ^ d ^ bcd;
    q[1] = ab ^ ac ^ bc ^ d ^ bd ^ abd;
    q[2] = ab ^ c ^ ac ^ d ^ ad ^ acd;
    q[3] = b ^ c ^ d ^ ad ^ bd ^ cd ^ bcd;
}

static inline void aes_field_inv8(uint32_t q[8]) {
    /* In the AES polynomial basis, choose t = 0x5c and u = 0xa2.
     * t^4 + t + 1 = 0 and u^2 + u = t^3. The tower basis
     * (1,t,t^2,t^3,u,ut,ut^2,ut^3) is
     * (01,5c,e0,50,a2,02,b8,db). The XOR maps here and below are
     * the inverse and forward binary matrices of these eight columns. */
    uint32_t a[4] = {
        q[0] ^ q[5] ^ q[7],
        q[2],
        q[2] ^ q[3] ^ q[4] ^ q[5] ^ q[6] ^ q[7],
        q[3] ^ q[4]
    };
    uint32_t b[4] = {
        q[4] ^ q[5] ^ q[6],
        q[1] ^ q[4] ^ q[6] ^ q[7],
        q[2] ^ q[3] ^ q[5] ^ q[7],
        q[5] ^ q[7]
    };
    uint32_t norm[4], v[8];
    /* (a + bu)^-1 = ((a + b) + bu) / (a^2 + ab + t^3*b^2).
     * Squaring and multiplication by t^3 are linear in GF(16). */
    aes_field_mul4(norm, a, b);
    norm[0] ^= a[0] ^ a[2] ^ b[2];
    norm[1] ^= a[2] ^ b[1] ^ b[2] ^ b[3];
    norm[2] ^= a[1] ^ a[3] ^ b[1];
    norm[3] ^= a[3] ^ b[0] ^ b[2] ^ b[3];
    aes_field_inv4(norm);
    for (unsigned i = 0; i < 4; i++)
        a[i] ^= b[i];
    aes_field_mul4(v, a, norm);
    aes_field_mul4(v + 4, b, norm);
    q[0] = v[0] ^ v[7];
    q[1] = v[4] ^ v[5] ^ v[7];
    q[2] = v[1];
    q[3] = v[1] ^ v[6] ^ v[7];
    q[4] = v[1] ^ v[3] ^ v[6] ^ v[7];
    q[5] = v[2] ^ v[4] ^ v[6];
    q[6] = v[1] ^ v[2] ^ v[3] ^ v[7];
    q[7] = v[2] ^ v[4] ^ v[6] ^ v[7];
}

static inline void aes_field_sbox(uint32_t q[8]) {
    uint32_t out[8];
    aes_field_inv8(q);
    /* S(x) = x^-1 ^ rotl(x^-1,1) ^ ... ^ rotl(x^-1,4) ^ 0x63. */
    for (unsigned i = 0; i < 8; i++)
        out[i] = q[i] ^ q[(i + 4) & 7] ^ q[(i + 5) & 7] ^
                 q[(i + 6) & 7] ^ q[(i + 7) & 7] ^
                 (0u - ((0x63u >> i) & 1u));
    for (unsigned i = 0; i < 8; i++)
        q[i] = out[i];
}

static inline void aes_field_inv_sbox(uint32_t q[8]) {
    uint32_t out[8];
    /* Undo the affine map before inversion: rotl(x,1) ^ rotl(x,3)
     * ^ rotl(x,6) ^ 0x05. */
    for (unsigned i = 0; i < 8; i++)
        out[i] = q[(i + 7) & 7] ^ q[(i + 5) & 7] ^ q[(i + 2) & 7] ^
                 (0u - ((0x05u >> i) & 1u));
    aes_field_inv8(out);
    for (unsigned i = 0; i < 8; i++)
        q[i] = out[i];
}

#endif /* NEVERC_AES_CT_FIELD_H */
