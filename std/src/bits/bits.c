#include "neverc/bits.h"

/*
 * Bit manipulation functions — mirrors Go math/bits package.
 * All pure-computation, no libc dependency.
 * Algorithms from Hacker's Delight (Warren) and de Bruijn sequences.
 */

static const uint8_t len8tab[256] = {
    0,1,2,2,3,3,3,3,4,4,4,4,4,4,4,4,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
    6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
    8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,
};

/* de Bruijn tables for TrailingZeros */
static const uint32_t deBruijn32 = 0x077CB531U;
static const uint8_t deBruijn32tab[32] = {
    0,1,28,2,29,14,24,3,30,22,20,15,25,17,4,8,
    31,27,13,23,21,19,16,7,26,12,18,6,11,5,10,9,
};

static const uint64_t deBruijn64 = 0x03f79d71b4ca8b09ULL;
static const uint8_t deBruijn64tab[64] = {
    0,1,56,2,57,49,28,3,61,58,42,50,38,29,17,4,
    62,47,59,36,45,43,51,22,53,39,33,30,24,18,12,5,
    63,55,48,27,60,41,37,16,46,35,44,21,52,32,23,11,
    54,26,40,15,34,20,31,10,25,14,19,9,13,8,7,6,
};

/* --- Len (bit length) --- */

int neverc_bits_len32(uint32_t x) {
    int n = 0;
    if (x >= (1U << 16)) { x >>= 16; n = 16; }
    if (x >= (1U << 8))  { x >>= 8;  n += 8; }
    return n + (int)len8tab[x];
}

int neverc_bits_len64(uint64_t x) {
    int n = 0;
    if (x >= (1ULL << 32)) { x >>= 32; n = 32; }
    return n + neverc_bits_len32((uint32_t)x);
}

/* --- Leading Zeros --- */

int neverc_bits_leading_zeros32(uint32_t x) {
    return 32 - neverc_bits_len32(x);
}

int neverc_bits_leading_zeros64(uint64_t x) {
    return 64 - neverc_bits_len64(x);
}

/* --- Trailing Zeros (de Bruijn) --- */

int neverc_bits_trailing_zeros32(uint32_t x) {
    if (x == 0) return 32;
    return (int)deBruijn32tab[(x & -x) * deBruijn32 >> 27];
}

int neverc_bits_trailing_zeros64(uint64_t x) {
    if (x == 0) return 64;
    return (int)deBruijn64tab[(x & -x) * deBruijn64 >> 58];
}

/* --- OnesCount (Hacker's Delight parallel summation) --- */

int neverc_bits_ones_count32(uint32_t x) {
    x = x - ((x >> 1) & 0x55555555U);
    x = (x & 0x33333333U) + ((x >> 2) & 0x33333333U);
    x = (x + (x >> 4)) & 0x0F0F0F0FU;
    return (int)((x * 0x01010101U) >> 24);
}

int neverc_bits_ones_count64(uint64_t x) {
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
}

/* --- Rotate Left --- */

uint32_t neverc_bits_rotate_left32(uint32_t x, int k) {
    unsigned s = (unsigned)k & 31;
    return (x << s) | (x >> (32 - s));
}

uint64_t neverc_bits_rotate_left64(uint64_t x, int k) {
    unsigned s = (unsigned)k & 63;
    return (x << s) | (x >> (64 - s));
}

/* --- Bit Reverse --- */

uint32_t neverc_bits_reverse32(uint32_t x) {
    x = ((x >> 1) & 0x55555555U) | ((x & 0x55555555U) << 1);
    x = ((x >> 2) & 0x33333333U) | ((x & 0x33333333U) << 2);
    x = ((x >> 4) & 0x0F0F0F0FU) | ((x & 0x0F0F0F0FU) << 4);
    return neverc_bits_reverse_bytes32(x);
}

uint64_t neverc_bits_reverse64(uint64_t x) {
    x = ((x >> 1) & 0x5555555555555555ULL) | ((x & 0x5555555555555555ULL) << 1);
    x = ((x >> 2) & 0x3333333333333333ULL) | ((x & 0x3333333333333333ULL) << 2);
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL) | ((x & 0x0F0F0F0F0F0F0F0FULL) << 4);
    return neverc_bits_reverse_bytes64(x);
}

/* --- Byte Swap --- */

uint16_t neverc_bits_reverse_bytes16(uint16_t x) {
    return (x >> 8) | (x << 8);
}

uint32_t neverc_bits_reverse_bytes32(uint32_t x) {
    return ((x >> 24)) |
           ((x >> 8) & 0x0000FF00U) |
           ((x << 8) & 0x00FF0000U) |
           ((x << 24));
}

uint64_t neverc_bits_reverse_bytes64(uint64_t x) {
    return ((x >> 56)) |
           ((x >> 40) & 0x000000000000FF00ULL) |
           ((x >> 24) & 0x0000000000FF0000ULL) |
           ((x >>  8) & 0x00000000FF000000ULL) |
           ((x <<  8) & 0x000000FF00000000ULL) |
           ((x << 24) & 0x0000FF0000000000ULL) |
           ((x << 40) & 0x00FF000000000000ULL) |
           ((x << 56));
}

/* --- Multi-precision arithmetic --- */

void neverc_bits_add64(uint64_t x, uint64_t y, uint64_t carry,
                       uint64_t *sum, uint64_t *carry_out) {
    uint64_t s = x + y + carry;
    *sum = s;
    *carry_out = ((x & y) | ((x | y) & ~s)) >> 63;
}

void neverc_bits_sub64(uint64_t x, uint64_t y, uint64_t borrow,
                       uint64_t *diff, uint64_t *borrow_out) {
    uint64_t d = x - y - borrow;
    *diff = d;
    *borrow_out = ((~x & y) | (~(x ^ y) & d)) >> 63;
}

void neverc_bits_mul64(uint64_t x, uint64_t y,
                       uint64_t *hi, uint64_t *lo) {
    uint64_t x0 = x & 0xFFFFFFFFULL;
    uint64_t x1 = x >> 32;
    uint64_t y0 = y & 0xFFFFFFFFULL;
    uint64_t y1 = y >> 32;
    uint64_t w0 = x0 * y0;
    uint64_t t = x1 * y0 + (w0 >> 32);
    uint64_t w1 = t & 0xFFFFFFFFULL;
    uint64_t w2 = t >> 32;
    w1 += x0 * y1;
    *hi = x1 * y1 + w2 + (w1 >> 32);
    *lo = x * y;
}
